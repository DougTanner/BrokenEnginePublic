# Private-LAN Firewall Rule for Development Servers

## Context

Launching a server executable from a new worktree can trigger a Windows Defender Firewall prompt because each worktree has a distinct executable path. The server listens on all interfaces through ENet at UDP port 27015 (`engine::Server::Server`, `Engine/Source/Network/Server/Server.cpp:17-25`), and the LAN discovery responder binds `INADDR_ANY` at UDP port 27016 (`NetworkDiscoveryResponder::NetworkDiscoveryResponder`, `Engine/Source/Network/NetworkDiscoveryResponder.cpp:10-23`). The agent command ports are loopback-only and do not need a firewall exception.

Allowing each worktree executable separately is repetitive. Disabling Defender Firewall or its notifications globally hides unrelated blocks and is not an acceptable development workflow.

## Design

- Add an opt-in Windows 11 firewall setup section to the agent-harness skill. The harness never changes firewall state automatically or requests elevation during a run.
- Document one idempotent elevated-PowerShell install command for a stable named inbound rule covering UDP local ports 27015 and 27016, `Profile Private`, and `RemoteAddress LocalSubnet`. This allows clients on the same private Wi-Fi/subnet while avoiding Public-profile and Internet-wide exposure.
- Document the matching exact-name removal command and a read-only inspection command that reports whether the rule is installed and active for the current network profile.
- Explain that Windows Developer Mode does not suppress Defender Firewall prompts, app rules are executable-path-specific, and the stable port rule is intended only for a trusted development machine/network.
- Have the harness preflight report a missing rule when LAN access is requested, with the install command, but continue normally for same-machine loopback verification.

## Critical files

- `.agents/skills/agent-harness/SKILL.md` — opt-in install/remove/inspect workflow and LAN-versus-loopback behavior.
- `Engine/Source/Network/NetworkProtocol.h` — authoritative game and discovery port constants; no code change expected.
- `Engine/Source/Network/Server/Server.cpp` and `Engine/Source/Network/NetworkDiscoveryResponder.cpp` — verify both listeners remain UDP and all-interface before publishing the rule.

## Out of scope

- Disabling Defender Firewall, disabling security notifications globally, or allowing Public-profile/WAN traffic.
- Automatically elevating from AgentCli, the harness, the game, or the server.
- Changing ENet/discovery bind addresses, network protocol ports, agent loopback ports, or production/Azure firewall policy.
- Creating executable-path rules for every worktree.

## Acceptance criteria

- The documented install is idempotent and produces one enabled inbound rule limited to UDP 27015/27016, Private profile, and LocalSubnet remote addresses.
- After installation, servers launched from different worktree paths do not produce new firewall prompts, and a client on the same private Wi-Fi/subnet can discover and connect to the server.
- The rule remains inactive on Public-profile networks, and the documented removal command deletes only the named Broken Engine development rule.
- Same-machine agent-harness verification remains usable without installing the LAN rule and never mutates firewall state.

## Notes

- Microsoft recommends allowing an app instead of opening a port in general; the path-independent port rule is chosen here specifically because worktree executable paths are ephemeral. Private-profile plus LocalSubnet scope is mandatory mitigation.
- Official references: Microsoft Defender Firewall notification settings, firewall allow-rule risk guidance, and `New-NetFirewallRule` documentation.
- Developer tooling and documentation only. No deterministic simulation, CRC, replay, wire-format, `kiVersion`, `.pack`, client/server guard, shader, or allocation-tracked-path exposure.
