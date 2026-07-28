[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $AgentHarness,

	[Parameter(Mandatory = $true)]
	[string] $Owner,

	[Parameter(Mandatory = $true)]
	[ValidateRange(1, 65535)]
	[int] $ClientPort,

	[ValidateRange(1, 600)]
	[int] $TimeoutSeconds = 60,

	[Parameter(Mandatory = $true)]
	[string] $ArtifactPath
)

$ErrorActionPreference = 'Stop'
$PollMilliseconds = 250
$Stopwatch = [Diagnostics.Stopwatch]::StartNew()
$PingAttempts = 0
$SceneAttempts = 0
$ClientTick = $null
$ReadyScene = $null
$LastObservation = 'No command attempted.'

function Test-JsonInteger($Value)
{
	return $Value -is [sbyte] -or $Value -is [byte] -or
		$Value -is [int16] -or $Value -is [uint16] -or
		$Value -is [int32] -or $Value -is [uint32] -or
		$Value -is [int64] -or $Value -is [uint64]
}

function Test-JsonNumber($Value)
{
	if ($Value -is [single] -or $Value -is [double])
	{
		return [double]::IsFinite([double]$Value)
	}

	return (Test-JsonInteger $Value) -or $Value -is [Numerics.BigInteger] -or $Value -is [decimal]
}

function Test-JsonNumericPair($Value)
{
	return $Value -is [System.Array] -and $Value.Count -eq 2 -and
		(Test-JsonNumber $Value[0]) -and (Test-JsonNumber $Value[1])
}

function ConvertTo-CanonicalValue($Value)
{
	if ($null -eq $Value -or $Value -is [string] -or $Value.GetType().IsPrimitive)
	{
		return $Value
	}

	if ($Value -is [pscustomobject])
	{
		$Names = [string[]]@($Value.PSObject.Properties.Name)
		[Array]::Sort($Names, [StringComparer]::Ordinal)
		$Result = [ordered]@{}
		foreach ($Name in $Names)
		{
			$Result[$Name] = ConvertTo-CanonicalValue $Value.$Name
		}
		return [pscustomobject]$Result
	}

	if ($Value -is [System.Collections.IEnumerable])
	{
		$Items = @()
		foreach ($Item in $Value)
		{
			$Items += ,(ConvertTo-CanonicalValue $Item)
		}
		return ,$Items
	}

	return $Value
}

function Invoke-AgentHarnessCommand($Request, [int] $TimeoutMilliseconds)
{
	$RequestJson = $Request | ConvertTo-Json -Depth 16 -Compress
	$CommandOutput = $RequestJson | & $script:ResolvedAgentHarness --owner $Owner --port $ClientPort `
		--timeout-ms $TimeoutMilliseconds -
	$CommandExit = $LASTEXITCODE
	$RawOutput = [string]::Join([Environment]::NewLine, @($CommandOutput))
	if ($CommandExit -ne 0)
	{
		return [pscustomobject]@{
			Valid = $false
			Detail = "AgentHarness exit $CommandExit."
			Response = $null
		}
	}

	try
	{
		$Response = $RawOutput | ConvertFrom-Json -Depth 100
	}
	catch
	{
		return [pscustomobject]@{
			Valid = $false
			Detail = "AgentHarness returned invalid JSON: $($_.Exception.Message)"
			Response = $null
		}
	}

	if ($Response.ok -isnot [bool] -or -not $Response.ok)
	{
		return [pscustomobject]@{
			Valid = $false
			Detail = 'AgentHarness response did not contain ok:true.'
			Response = $Response
		}
	}

	return [pscustomobject]@{
		Valid = $true
		Detail = 'ok:true'
		Response = $Response
	}
}

function Get-RemainingMilliseconds()
{
	return [Math]::Max(0, ($TimeoutSeconds * 1000) - [int]$Stopwatch.ElapsedMilliseconds)
}

function Wait-ForNextPoll([long] $NextPollMilliseconds)
{
	$Delay = $NextPollMilliseconds - $Stopwatch.ElapsedMilliseconds
	if ($Delay -gt 0)
	{
		Start-Sleep -Milliseconds ([int][Math]::Min($Delay, (Get-RemainingMilliseconds)))
	}
}

function Write-ResultArtifact([string] $Status, [string] $Code, [string] $Phase,
	[string] $Message, [bool] $Ready)
{
	$Artifact = [ordered]@{
		SchemaVersion = 'broken-engine-island-scene-readiness/v1'
		Status = $Status
		Code = $Code
		Phase = $Phase
		Ready = $Ready
		ClientPort = $ClientPort
		TimeoutSeconds = $TimeoutSeconds
		ElapsedMilliseconds = [long]$Stopwatch.ElapsedMilliseconds
		PingAttempts = [int]$PingAttempts
		SceneAttempts = [int]$SceneAttempts
		ClientTick = $ClientTick
		Scene = $ReadyScene
		Message = $Message
	}
	$ArtifactJson = $Artifact | ConvertTo-Json -Depth 100
	$ArtifactDirectory = Split-Path -Parent $script:ResolvedArtifactPath
	if ($ArtifactDirectory)
	{
		[IO.Directory]::CreateDirectory($ArtifactDirectory) | Out-Null
	}
	[IO.File]::WriteAllText($script:ResolvedArtifactPath, ($ArtifactJson + [Environment]::NewLine),
		[Text.UTF8Encoding]::new($false))
	[Console]::Out.Write($ArtifactJson)
}

try
{
	if ([string]::IsNullOrWhiteSpace($Owner))
	{
		throw 'Owner must not be empty.'
	}
	$ResolvedAgentHarnessPath = (Resolve-Path -LiteralPath $AgentHarness).Path
	if (-not (Test-Path -LiteralPath $ResolvedAgentHarnessPath -PathType Leaf))
	{
		throw "AgentHarness path is not a file: '$ResolvedAgentHarnessPath'."
	}
	$script:ResolvedAgentHarness = $ResolvedAgentHarnessPath
	$script:ResolvedArtifactPath = [IO.Path]::GetFullPath($ArtifactPath)

	$NextPoll = 0L
	while ((Get-RemainingMilliseconds) -gt 0)
	{
		Wait-ForNextPoll $NextPoll
		if ((Get-RemainingMilliseconds) -le 0)
		{
			break
		}
		$NextPoll += $PollMilliseconds
		$PingAttempts++
		$CommandTimeout = [Math]::Max(1, [Math]::Min($PollMilliseconds, (Get-RemainingMilliseconds)))
		$Ping = Invoke-AgentHarnessCommand ([ordered]@{ cmd = 'ping' }) $CommandTimeout
		if (-not $Ping.Valid)
		{
			$LastObservation = "Ping attempt $PingAttempts failed: $($Ping.Detail)"
			continue
		}

		$PingResult = $Ping.Response.result
		if ($PingResult.build -cne 'client' -or -not (Test-JsonInteger $PingResult.tick) -or
			[long]$PingResult.tick -lt 0)
		{
			$LastObservation = "Ping attempt $PingAttempts was not client-ready."
			continue
		}

		$ClientTick = [long]$PingResult.tick
		break
	}

	if ($null -eq $ClientTick)
	{
		Write-ResultArtifact 'failure' 'timeout' 'ping' `
			"Client did not return a ready ping before timeout. $LastObservation" $false
		exit 1
	}

	$Remaining = Get-RemainingMilliseconds
	if ($Remaining -le 0)
	{
		Write-ResultArtifact 'failure' 'timeout' 'window_state' `
			'Client became ready after the timeout expired.' $false
		exit 1
	}
	$Window = Invoke-AgentHarnessCommand ([ordered]@{
		cmd = 'window_state'
		params = [ordered]@{ minimized = $false }
	}) $Remaining
	if (-not $Window.Valid -or $Window.Response.result.minimized -isnot [bool] -or
		$Window.Response.result.minimized)
	{
		$WindowDetail = if ($Window.Valid) { 'Response did not confirm minimized:false.' } else { $Window.Detail }
		Write-ResultArtifact 'failure' 'window-state-failed' 'window_state' `
			"Unable to keep client visible: $WindowDetail" $false
		exit 1
	}

	$PreviousCanonicalScene = $null
	$NextPoll = $Stopwatch.ElapsedMilliseconds
	while ((Get-RemainingMilliseconds) -gt 0)
	{
		Wait-ForNextPoll $NextPoll
		if ((Get-RemainingMilliseconds) -le 0)
		{
			break
		}
		$NextPoll += $PollMilliseconds
		$SceneAttempts++
		$CommandTimeout = [Math]::Max(1, [Math]::Min($PollMilliseconds, (Get-RemainingMilliseconds)))
		$SceneCommand = Invoke-AgentHarnessCommand ([ordered]@{
			cmd = 'describe_scene'
			params = [ordered]@{ includeUnits = $false; maxUnits = 0 }
		}) $CommandTimeout
		if (-not $SceneCommand.Valid)
		{
			$LastObservation = "Scene attempt $SceneAttempts failed: $($SceneCommand.Detail)"
			$PreviousCanonicalScene = $null
			continue
		}

		$SceneResult = $SceneCommand.Response.result
		$HasGridCoord = $SceneResult.PSObject.Properties.Name -ccontains 'clientGridCoord' -and
			(Test-JsonNumericPair $SceneResult.clientGridCoord)
		$HasIslands = $SceneResult.PSObject.Properties.Name -ccontains 'islands'
		$CompleteFootprints = $HasIslands -and $SceneResult.islands -is [System.Array] -and
			$SceneResult.islands.Count -gt 0
		if ($CompleteFootprints)
		{
			foreach ($Island in $SceneResult.islands)
			{
				$HasFootprint = $null -ne $Island -and
					$Island.PSObject.Properties.Name -ccontains 'footprint' -and
					(Test-JsonNumericPair $Island.footprint)
				if (-not $HasFootprint)
				{
					$CompleteFootprints = $false
					break
				}
			}
		}

		if (-not $HasGridCoord -or -not $CompleteFootprints)
		{
			$LastObservation = "Scene attempt $SceneAttempts did not contain a numeric client grid coordinate and nonempty islands with numeric-pair footprints."
			$PreviousCanonicalScene = $null
			continue
		}

		$ProjectedScene = [pscustomobject][ordered]@{
			clientGridCoord = $SceneResult.clientGridCoord
			islands = $SceneResult.islands
		}
		$CanonicalScene = ConvertTo-CanonicalValue $ProjectedScene
		$CanonicalJson = $CanonicalScene | ConvertTo-Json -Depth 100 -Compress
		if ($null -ne $PreviousCanonicalScene -and $CanonicalJson -ceq $PreviousCanonicalScene)
		{
			$ReadyScene = $CanonicalScene
			Write-ResultArtifact 'success' 'ready' 'complete' `
				'Client island scene is complete and stable across two consecutive samples.' $true
			exit 0
		}

		$PreviousCanonicalScene = $CanonicalJson
		$LastObservation = "Scene attempt $SceneAttempts was complete but not yet stable."
	}

	Write-ResultArtifact 'failure' 'timeout' 'describe_scene' `
		"Client island scene did not stabilize before timeout. $LastObservation" $false
	exit 1
}
catch
{
	try
	{
		if (-not $script:ResolvedArtifactPath)
		{
			$script:ResolvedArtifactPath = [IO.Path]::GetFullPath($ArtifactPath)
		}
		Write-ResultArtifact 'failure' 'error' 'setup' $_.Exception.Message $false
	}
	catch
	{
		[Console]::Error.WriteLine($_.Exception.Message)
	}
	exit 1
}
