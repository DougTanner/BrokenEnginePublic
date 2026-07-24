# Private-LAN Firewall

Read this reference only when a requested scenario explicitly needs a client on another machine or private Wi-Fi to reach the server UDP game listener `27015` and discovery listener `27016`. Cross-machine launches omit `--loopback-only`. Agent command channels remain on `127.0.0.1` and never need a firewall exception.

Firewall changes are operator-driven. Never execute the install/removal blocks, request elevation, disable firewall/notifications, change a network category, or add an executable-path/Public-profile rule. Give the operator this exact-name, idempotent elevated-PowerShell install block:

```powershell
$RuleName = 'BrokenEngine-PrivateLan-UDP'
$DisplayName = 'Broken Engine Private-LAN UDP'

Remove-NetFirewallRule -PolicyStore PersistentStore -Name $RuleName -ErrorAction SilentlyContinue
New-NetFirewallRule -PolicyStore PersistentStore -Name $RuleName -DisplayName $DisplayName `
	-Enabled True -Direction Inbound -Action Allow -Profile Private -Protocol UDP `
	-LocalPort 27015,27016 -RemoteAddress LocalSubnet
```

Windows-generated per-executable `Query User` inbound block rules override this allow rule. Before a cross-machine launch, run the bundled read-only, non-elevated check with every exact executable path:

```powershell
& "$ROOT\.agents\skills\agent-harness\scripts\Test-PrivateLanFirewallReadiness.ps1" `
	-ExecutablePath '<exact client executable path>','<exact server executable path>'
$ReadinessExit = $LASTEXITCODE
```

The JSON result includes resolved executables and blockers, local `PersistentStore` and traced resultant `ActiveStore` shapes, Private profile/connections, `Ready`, and `ReadinessReason`. `ActiveStore` presence or `PrimaryStatus` alone does not prove enforcement. Exit `0` means the exact local rule is fully enforced and no supplied executable has an enabled inbound `Query User` blocker; `2` means absent, mismatched, overridden, unenforced, or blocked; `1` means invocation or query failure. Blocker output is diagnostic, not removal authority. Use `-RuleName` only for a deliberately different exact name.

On a non-ready result, report its exact reason and the install block without executing it. Continue loopback-only only when that satisfies acceptance; otherwise return the prerequisite `BLOCKED`. Group Policy may disable local-rule merge.

For opt-out, give the operator this exact elevated-PowerShell removal command:

```powershell
Remove-NetFirewallRule -PolicyStore PersistentStore -Name 'BrokenEngine-PrivateLan-UDP' -ErrorAction SilentlyContinue
```

The exception is limited to inbound UDP `27015,27016`, `LocalSubnet`, and the Private profile. It covers neither Public nor WAN traffic. App allowances are normally preferable but are worktree-path-specific; Developer Mode does not authorize arbitrary development executables.

Primary references: firewall allowance risks (`https://support.microsoft.com/en-us/windows/security/firewall/risks-of-allowing-apps-through-windows-firewall`), Windows Firewall rule guidance (`https://learn.microsoft.com/en-us/windows/security/operating-system-security/network-security/windows-firewall/rules`), `New-NetFirewallRule` (`https://learn.microsoft.com/en-us/powershell/module/netsecurity/new-netfirewallrule?view=windowsserver2025-ps`), `Get-NetFirewallRule` (`https://learn.microsoft.com/en-us/powershell/module/netsecurity/get-netfirewallrule?view=windowsserver2025-ps`), `MSFT_NetFirewallRule.EnforcementStatus` (`https://learn.microsoft.com/en-us/windows/win32/fwp/wmi/wfascimprov/msft-netfirewallrule`), and `MSFT_NetFirewallProfile.AllowLocalFirewallRules` (`https://learn.microsoft.com/en-us/windows/win32/fwp/wmi/wfascimprov/msft-netfirewallprofile`).
