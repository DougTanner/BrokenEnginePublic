[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Assert-True([bool] $Condition, [string] $Message)
{
	if (-not $Condition)
	{
		throw $Message
	}
}

$FixtureRoot = Join-Path ([IO.Path]::GetTempPath()) `
	('BrokenEngine-WaitIslandSceneReady-' + [Guid]::NewGuid().ToString('N'))
$MockHarness = Join-Path $FixtureRoot 'Mock-AgentHarness.ps1'
$StatePath = Join-Path $FixtureRoot 'state.txt'
$SuccessArtifact = Join-Path $FixtureRoot 'success.json'
$FailureArtifact = Join-Path $FixtureRoot 'failure.json'
$MalformedArtifact = Join-Path $FixtureRoot 'malformed.json'
$Helper = Join-Path $PSScriptRoot 'Wait-IslandSceneReady.ps1'
$PowerShell = Join-Path $PSHOME 'pwsh.exe'

$MockSource = @'
[CmdletBinding(PositionalBinding = $false)]
param(
	[Parameter(ValueFromPipeline = $true)]
	[string] $InputJson,

	[Parameter(ValueFromRemainingArguments = $true)]
	[string[]] $HarnessArguments
)

$ErrorActionPreference = 'Stop'
$Request = $InputJson | ConvertFrom-Json -Depth 100
if ($HarnessArguments.Count -ne 7 -or $HarnessArguments[0] -cne '--owner' -or
	$HarnessArguments[1] -cne 'fixture-owner' -or $HarnessArguments[2] -cne '--port' -or
	$HarnessArguments[3] -cne '27101' -or $HarnessArguments[4] -cne '--timeout-ms' -or
	$HarnessArguments[6] -cne '-')
{
	throw "Unexpected AgentHarness arguments: $($HarnessArguments -join ' ')"
}

$Call = if (Test-Path -LiteralPath $env:WAIT_ISLAND_STATE) {
	[int](Get-Content -Raw -LiteralPath $env:WAIT_ISLAND_STATE)
} else {
	0
}
$Call++
[IO.File]::WriteAllText($env:WAIT_ISLAND_STATE, "$Call", [Text.UTF8Encoding]::new($false))

if ($env:WAIT_ISLAND_MODE -ceq 'timeout')
{
	if ($Request.cmd -cne 'ping') { throw "Unexpected timeout command '$($Request.cmd)'." }
	exit 1
}

if ($env:WAIT_ISLAND_MODE -ceq 'malformed' -and $Request.cmd -ceq 'describe_scene')
{
		$Response = [ordered]@{
			id = $null; ok = $true; result = [ordered]@{
				clientGridCoord = $null
				islands = @([ordered]@{ coord = @(0, 0); center = @(10, 20); rotation = 0.0; footprint = @($null) })
			}
		}
	}
	else
	{
		switch ($Call)
		{
			1
			{
				if ($Request.cmd -cne 'ping') { throw "Expected ping, got '$($Request.cmd)'." }
				exit 1
			}
			2
			{
				$Response = [ordered]@{ id = $null; ok = $true; result = [ordered]@{ build = 'client'; tick = -1 } }
			}
			3
			{
				$Response = [ordered]@{ id = $null; ok = $true; result = [ordered]@{ build = 'client'; tick = 7 } }
			}
			4
			{
				if ($Request.cmd -cne 'window_state' -or $Request.params.minimized -isnot [bool] -or
					$Request.params.minimized)
				{
					throw 'Expected window_state {minimized:false}.'
				}
				$Response = [ordered]@{ id = $null; ok = $true; result = [ordered]@{ minimized = $false; width = 1600; height = 900 } }
			}
			5
			{
				$Response = [ordered]@{
					id = $null; ok = $true; result = [ordered]@{
						clientGridCoord = @(0, 0)
						islands = @([ordered]@{ coord = @(0, 0); center = @(10, 20); rotation = 0.0 })
					}
				}
			}
			6
			{
				$Response = [ordered]@{
					id = $null; ok = $true; result = [ordered]@{
						clientGridCoord = @(0, 0)
						islands = @(
							[ordered]@{ coord = @(0, 0); center = @(10, 20); rotation = 0.0; footprint = @(100, 200) },
							[ordered]@{ coord = @(1, 0); center = @(30, 40); rotation = 0.5; footprint = @(80, 90) }
						)
					}
				}
			}
			7
			{
				$Response = [ordered]@{
					id = $null; ok = $true; result = [ordered]@{
						clientGridCoord = @(0, 0)
						islands = @(
							[ordered]@{ rotation = 0.5; footprint = @(80, 90); center = @(30, 40); coord = @(1, 0) },
							[ordered]@{ rotation = 0.0; footprint = @(100, 200); center = @(10, 20); coord = @(0, 0) }
						)
					}
				}
			}
			8
			{
				$Response = [ordered]@{
					id = $null; ok = $true; result = [ordered]@{
						islands = @(
							[ordered]@{ coord = @(1, 0); center = @(30, 40); footprint = @(80, 90); rotation = 0.5 },
							[ordered]@{ coord = @(0, 0); center = @(10, 20); footprint = @(100, 200); rotation = 0.0 }
						)
						clientGridCoord = @(0, 0)
					}
				}
			}
			default
			{
				throw "Unexpected call $Call."
			}
		}
	}

if ($Call -ge 5 -and ($Request.cmd -cne 'describe_scene' -or
	$Request.params.includeUnits -isnot [bool] -or $Request.params.includeUnits -or
	$Request.params.maxUnits -ne 0))
{
	throw 'Expected describe_scene {includeUnits:false,maxUnits:0}.'
}
Write-Output ($Response | ConvertTo-Json -Depth 20 -Compress)
exit 0
'@

try
{
	[IO.Directory]::CreateDirectory($FixtureRoot) | Out-Null
	[IO.File]::WriteAllText($MockHarness, $MockSource, [Text.UTF8Encoding]::new($false))

	$env:WAIT_ISLAND_STATE = $StatePath
	$env:WAIT_ISLAND_MODE = 'success'
	$SuccessOutput = & $PowerShell -NoProfile -File $Helper -AgentHarness $MockHarness `
		-Owner 'fixture-owner' -ClientPort 27101 -TimeoutSeconds 5 -ArtifactPath $SuccessArtifact
	$SuccessExit = $LASTEXITCODE
	Assert-True ($SuccessExit -eq 0) "Success fixture exited $SuccessExit`: $SuccessOutput"
	Assert-True (Test-Path -LiteralPath $SuccessArtifact -PathType Leaf) 'Success artifact was not written.'
	$Success = Get-Content -Raw -LiteralPath $SuccessArtifact | ConvertFrom-Json -Depth 100
	Assert-True ($Success.SchemaVersion -ceq 'broken-engine-island-scene-readiness/v1') 'Unexpected success schema.'
	Assert-True ($Success.Status -ceq 'success' -and $Success.Code -ceq 'ready' -and
		$Success.Phase -ceq 'complete' -and $Success.Ready -is [bool] -and $Success.Ready) `
		'Success artifact did not report typed ready state.'
	Assert-True ($Success.PingAttempts -eq 3 -and $Success.SceneAttempts -eq 4 -and
		$Success.ClientTick -eq 7) 'Success attempt counts or tick were unexpected.'
	Assert-True ($Success.Scene.islands.Count -eq 2 -and $Success.Scene.islands[0].coord[0] -eq 1 -and
		$Success.Scene.islands[1].coord[0] -eq 0) 'Canonical scene did not preserve stable island array order.'
	Assert-True ((Get-Content -Raw -LiteralPath $StatePath) -eq '8') 'Mock did not observe the expected command sequence.'

	[IO.File]::WriteAllText($StatePath, '0', [Text.UTF8Encoding]::new($false))
	$env:WAIT_ISLAND_MODE = 'timeout'
	$FailureOutput = & $PowerShell -NoProfile -File $Helper -AgentHarness $MockHarness `
		-Owner 'fixture-owner' -ClientPort 27101 -TimeoutSeconds 1 -ArtifactPath $FailureArtifact
	$FailureExit = $LASTEXITCODE
	Assert-True ($FailureExit -eq 1) "Timeout fixture exited $FailureExit`: $FailureOutput"
	Assert-True (Test-Path -LiteralPath $FailureArtifact -PathType Leaf) 'Failure artifact was not written.'
	$Failure = Get-Content -Raw -LiteralPath $FailureArtifact | ConvertFrom-Json -Depth 100
	Assert-True ($Failure.Status -ceq 'failure' -and $Failure.Code -ceq 'timeout' -and
		$Failure.Phase -ceq 'ping' -and $Failure.Ready -is [bool] -and -not $Failure.Ready) `
		'Failure artifact did not report typed timeout state.'
	Assert-True ($Failure.PingAttempts -gt 0 -and $Failure.SceneAttempts -eq 0 -and
		$null -eq $Failure.Scene) 'Failure artifact attempt counts or scene were unexpected.'

	[IO.File]::WriteAllText($StatePath, '0', [Text.UTF8Encoding]::new($false))
	$env:WAIT_ISLAND_MODE = 'malformed'
	$MalformedOutput = & $PowerShell -NoProfile -File $Helper -AgentHarness $MockHarness `
		-Owner 'fixture-owner' -ClientPort 27101 -TimeoutSeconds 1 -ArtifactPath $MalformedArtifact
	$MalformedExit = $LASTEXITCODE
	Assert-True ($MalformedExit -eq 1) "Malformed fixture exited $MalformedExit`: $MalformedOutput"
	Assert-True (Test-Path -LiteralPath $MalformedArtifact -PathType Leaf) 'Malformed artifact was not written.'
	$Malformed = Get-Content -Raw -LiteralPath $MalformedArtifact | ConvertFrom-Json -Depth 100
	Assert-True ($Malformed.Status -ceq 'failure' -and $Malformed.Code -ceq 'timeout' -and
		$Malformed.Phase -ceq 'describe_scene' -and $Malformed.Ready -is [bool] -and -not $Malformed.Ready) `
		'Malformed artifact did not report typed timeout state.'
	Assert-True ($Malformed.SceneAttempts -gt 1 -and $null -eq $Malformed.Scene) `
		'Malformed stable payload was accepted as ready.'

	[pscustomobject]@{
		SchemaVersion = 'broken-engine-island-scene-readiness-fixture/v1'
		Status = 'pass'
		Cases = @(
			'transport failure and pre-game ping retry'
			'visible window restore'
			'incomplete, unstable, then two canonical stable scene samples'
			'timeout failure artifact'
			'stable malformed coordinate and footprint rejection'
		)
	} | ConvertTo-Json -Depth 4 -Compress
}
finally
{
	Remove-Item Env:WAIT_ISLAND_STATE -ErrorAction SilentlyContinue
	Remove-Item Env:WAIT_ISLAND_MODE -ErrorAction SilentlyContinue
	if (Test-Path -LiteralPath $FixtureRoot)
	{
		Remove-Item -LiteralPath $FixtureRoot -Recurse -Force
	}
}
