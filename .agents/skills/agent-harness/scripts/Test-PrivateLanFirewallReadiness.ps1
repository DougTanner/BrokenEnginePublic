[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string[]] $ExecutablePath,

	[string] $RuleName = 'BrokenEngine-PrivateLan-UDP'
)

$ErrorActionPreference = 'Stop'
$DesiredPorts = @('27015', '27016')

function ConvertTo-ValueSet([object[]] $Values)
{
	return @($Values | ForEach-Object { "$_" -split ',' } |
		ForEach-Object { $_.Trim() } | Where-Object { $_ } | Sort-Object -Unique)
}

function Test-ValueSet([object[]] $Actual, [object[]] $Expected)
{
	return @(Compare-Object (ConvertTo-ValueSet $Actual) (ConvertTo-ValueSet $Expected)).Count -eq 0
}

function Get-RuleShape($Rule)
{
	$Port = Get-NetFirewallPortFilter -AssociatedNetFirewallRule $Rule
	$Address = Get-NetFirewallAddressFilter -AssociatedNetFirewallRule $Rule
	return [pscustomobject]@{
		Name = "$($Rule.Name)"
		Enabled = "$($Rule.Enabled)"
		Direction = "$($Rule.Direction)"
		Action = "$($Rule.Action)"
		Profile = @($Rule.Profile | ForEach-Object { "$_" })
		Protocol = @($Port.Protocol | ForEach-Object { "$_" })
		LocalPort = @($Port.LocalPort | ForEach-Object { "$_" })
		RemoteAddress = @($Address.RemoteAddress | ForEach-Object { "$_" })
		PolicyStoreSource = "$($Rule.PolicyStoreSource)"
		PolicyStoreSourceType = "$($Rule.PolicyStoreSourceType)"
		EnforcementStatus = @($Rule.EnforcementStatus | ForEach-Object { "$_" })
	}
}

function Test-DesiredShape($Shape)
{
	return $Shape.Name -eq $RuleName -and
		$Shape.Enabled -eq 'True' -and
		$Shape.Direction -eq 'Inbound' -and
		$Shape.Action -eq 'Allow' -and
		(Test-ValueSet $Shape.Profile @('Private')) -and
		(Test-ValueSet $Shape.Protocol @('UDP')) -and
		(Test-ValueSet $Shape.LocalPort $DesiredPorts) -and
		(Test-ValueSet $Shape.RemoteAddress @('LocalSubnet'))
}

function Get-InspectionStatus($LocalRules, $DesiredLocalShapes, $ActiveRules,
	$DesiredActiveLocalShapes, $PrivateProfile, $PrivateConnections)
{
	if (@($LocalRules).Count -eq 0)
	{
		return 'NOT INSTALLED: no exact-name rule exists in PersistentStore.'
	}
	if (@($LocalRules).Count -ne 1 -or @($DesiredLocalShapes).Count -ne 1)
	{
		return 'LOCAL RULE MISMATCH: the exact-name PersistentStore rule does not have the required shape.'
	}
	if (@($ActiveRules).Count -eq 0)
	{
		return 'NO RESULTANT RULE: the local rule exists but no exact-name rule reached ActiveStore.'
	}
	if (@($DesiredActiveLocalShapes).Count -ne 1)
	{
		return 'RESULTANT OVERRIDE/MISMATCH: inspect PolicyStoreSourceType and the filters above.'
	}
	if ("$($PrivateProfile.Enabled)" -ne 'True')
	{
		return 'NOT ENFORCED: FirewallOffInProfile.'
	}
	if ("$($PrivateProfile.AllowLocalFirewallRules)" -eq 'False')
	{
		return 'NOT ENFORCED: LocalFirewallRulesDisallowed.'
	}
	if (@($PrivateConnections).Count -eq 0)
	{
		return 'NOT ENFORCED: InactiveProfile (no current Private connection).'
	}
	$Statuses = ConvertTo-ValueSet $DesiredActiveLocalShapes.EnforcementStatus
	if (-not (Test-ValueSet $Statuses @('Enforced')))
	{
		return "NOT ENFORCED: $($Statuses -join ', ')."
	}
	return 'FULLY ENFORCED: exact local rule is resultant and enforced for an eligible Private connection.'
}

try
{
	if ([string]::IsNullOrWhiteSpace($RuleName))
	{
		throw 'RuleName must not be empty.'
	}
	if (@($ExecutablePath).Count -eq 0)
	{
		throw 'At least one executable path is required.'
	}

	$ResolvedExecutables = @($ExecutablePath | ForEach-Object {
		if ([string]::IsNullOrWhiteSpace($_))
		{
			throw 'Executable paths must not be empty.'
		}
		$Resolved = Resolve-Path -LiteralPath $_
		if (-not (Test-Path -LiteralPath $Resolved.Path -PathType Leaf))
		{
			throw "Executable path is not a file: '$($Resolved.Path)'."
		}
		[pscustomobject]@{
			RequestedPath = $_
			ResolvedPath = $Resolved.Path
		}
	})

	$QueryUserRules = @(Get-NetFirewallRule -PolicyStore ActiveStore -TracePolicyStore -Enabled True `
		-Direction Inbound -Action Block | Where-Object {
			$_.Name -like '*Query User*' -or $_.DisplayName -like '*Query User*'
	})
	$ExecutableDiagnostics = @($ResolvedExecutables | ForEach-Object {
		$Executable = $_
		$Blockers = @($QueryUserRules | ForEach-Object {
			$BlockRule = $_
			Get-NetFirewallApplicationFilter -AssociatedNetFirewallRule $BlockRule | Where-Object {
				[Environment]::ExpandEnvironmentVariables($_.Program) -ieq $Executable.ResolvedPath
			} | ForEach-Object {
				[pscustomobject]@{
					Name = "$($BlockRule.Name)"
					DisplayName = "$($BlockRule.DisplayName)"
					Action = "$($BlockRule.Action)"
					Program = "$($_.Program)"
					PolicyStoreSource = "$($BlockRule.PolicyStoreSource)"
				}
			}
		})
		[pscustomobject]@{
			RequestedPath = $Executable.RequestedPath
			ResolvedPath = $Executable.ResolvedPath
			QueryUserBlockers = $Blockers
		}
	})

	$LocalRules = @(Get-NetFirewallRule -PolicyStore PersistentStore -Name $RuleName -ErrorAction SilentlyContinue)
	$ActiveRules = @(Get-NetFirewallRule -PolicyStore ActiveStore -TracePolicyStore -Name $RuleName -ErrorAction SilentlyContinue)
	$LocalShapes = @($LocalRules | ForEach-Object { Get-RuleShape $_ })
	$ActiveShapes = @($ActiveRules | ForEach-Object { Get-RuleShape $_ })
	$PrivateProfile = Get-NetFirewallProfile -PolicyStore ActiveStore -Name Private
	$Connections = @(Get-NetConnectionProfile)
	$PrivateConnections = @($Connections | Where-Object NetworkCategory -eq 'Private')
	$DesiredLocalShapes = @($LocalShapes | Where-Object { Test-DesiredShape $_ })
	$DesiredActiveLocalShapes = @($ActiveShapes | Where-Object {
		(Test-DesiredShape $_) -and $_.PolicyStoreSourceType -eq 'Local' -and
		$_.PolicyStoreSource -eq 'PersistentStore'
	})
	$FirewallReason = Get-InspectionStatus $LocalRules $DesiredLocalShapes $ActiveRules `
		$DesiredActiveLocalShapes $PrivateProfile $PrivateConnections
	$BlockerCount = 0
	$ExecutableDiagnostics | ForEach-Object {
		$BlockerCount += @($_.QueryUserBlockers).Count
	}
	$FirewallReady = $FirewallReason -like 'FULLY ENFORCED:*'
	$Ready = $FirewallReady -and $BlockerCount -eq 0
	if ($Ready)
	{
		$ReadinessReason = "$FirewallReason Zero executable Query User blockers found."
	}
	else
	{
		$Reasons = @()
		if (-not $FirewallReady)
		{
			$Reasons += $FirewallReason
		}
		if ($BlockerCount -ne 0)
		{
			$Reasons += "EXECUTABLE BLOCKERS: $BlockerCount enabled inbound Query User block rule(s) match the supplied executable paths."
		}
		$ReadinessReason = $Reasons -join ' '
	}

	[pscustomobject]@{
		RuleName = $RuleName
		Executables = $ExecutableDiagnostics
		Firewall = [pscustomobject]@{
			LocalPersistentRules = $LocalShapes
			ResultantActiveRules = $ActiveShapes
			PrivateProfile = [pscustomobject]@{
				Name = "$($PrivateProfile.Name)"
				Enabled = "$($PrivateProfile.Enabled)"
				AllowLocalFirewallRules = "$($PrivateProfile.AllowLocalFirewallRules)"
			}
			CurrentConnections = @($Connections | ForEach-Object {
				[pscustomobject]@{
					Name = "$($_.Name)"
					InterfaceAlias = "$($_.InterfaceAlias)"
					NetworkCategory = "$($_.NetworkCategory)"
					IPv4Connectivity = "$($_.IPv4Connectivity)"
					IPv6Connectivity = "$($_.IPv6Connectivity)"
				}
			})
			ReadinessReason = $FirewallReason
		}
		ExecutableBlockerCount = $BlockerCount
		Ready = $Ready
		ReadinessReason = $ReadinessReason
	} | ConvertTo-Json -Depth 8

	if ($Ready)
	{
		exit 0
	}
	exit 2
}
catch
{
	[pscustomobject]@{
		RuleName = $RuleName
		Ready = $false
		ReadinessReason = "QUERY FAILURE: $($_.Exception.Message)"
	} | ConvertTo-Json -Depth 4
	exit 1
}
