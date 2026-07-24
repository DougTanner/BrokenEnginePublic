# Azure Hosting Guide — Broken Engine Server

Solo-developer setup for running `BrokenEngineSandboxServer.exe` on an Azure Windows VM: intermittent usage, deterministic replay, Visual Studio 2026 integration. Prices are approximate East US pay-as-you-go rates as of July 2026 — verify current numbers at the [Azure pricing calculator](https://azure.microsoft.com/en-us/pricing/calculator/).

## Contents

1. [Why Azure for this server](#1-why-azure-for-this-server)
2. [VM sizing and fixed costs](#2-vm-sizing-and-fixed-costs)
3. [Dev/Test offer — Windows at Linux rates](#3-devtest-offer--windows-at-linux-rates)
4. [Cost scenarios](#4-cost-scenarios)
5. [Step-by-step setup](#5-step-by-step-setup)
6. [Deploying the Broken Engine server](#6-deploying-the-broken-engine-server)
7. [Day-to-day workflow](#7-day-to-day-workflow)
8. [Visual Studio 2026 remote debugging](#8-visual-studio-2026-remote-debugging)
9. [Security and patching](#9-security-and-patching)
10. [Cost tips and cheat sheet](#10-cost-tips-and-cheat-sheet)

## 1. Why Azure for this server

- **Deallocate billing model.** Stop the VM from the Azure Portal and compute billing stops immediately. Disk, OS, installed software, and the deployed server stay intact — no snapshot/redeploy cycle. Ideal for a few hours of testing per day.
- **Static IP.** A Standard-SKU static public IP (~$3.65/month) survives deallocate/start cycles, so clients always connect to the same address.
- **First-party Windows + Visual Studio tooling.** Prebuilt Windows Server images deploy in minutes with licensing in the hourly rate; VS 2026 remote debugging attaches directly to the server process.
- **Determinism fit.** The simulation is engineered to be CPU-generation-independent: `/fp:strict` in every configuration, FMA3 disabled at startup, per-thread MXCSR pinned (flush-to-zero, round-to-nearest), no libm transcendentals in sim math (`Documents/FloatingPointDeterminism.txt`). The only hardware floor is **x64 with SSE4.1**, which every current Azure VM clears. What does matter: build client and server from the same source with the same VS 2026 toolchain — the handshake rejects mismatched protocol or Frame versions, and the per-tick CRC disconnects a desynced client.

## 2. VM sizing and fixed costs

The current general-purpose series is **Dsv6** (5th Gen Intel Xeon "Emerald Rapids", successor to Dsv5). The server is headless — no Vulkan, no GPU — so general-purpose sizes are correct. It caps at 64 peers, and a handful of test clients fits comfortably in 2 cores.

| Size | vCPUs | RAM | Linux $/hr | Windows PAYG $/hr | Windows PAYG $/mo (730 h) |
| --- | --- | --- | --- | --- | --- |
| D2s v6 | 2 | 8 GB | ~$0.10 | ~$0.19 | ~$141 |
| D4s v6 | 4 | 16 GB | ~$0.20 | ~$0.39 | ~$282 |
| D8s v6 | 8 | 32 GB | ~$0.40 | ~$0.77 | ~$563 |

The Windows surcharge is roughly $0.046/vCPU/hr on top of the Linux rate — see [§3](#3-devtest-offer--windows-at-linux-rates) for how to eliminate it entirely.

Fixed costs that bill regardless of VM state:

| Resource | Cost | Notes |
| --- | --- | --- |
| Standard SSD E10 (128 GB) | ~$9.60/mo | Billed even when deallocated; plenty for OS + server + `Data/` |
| Static public IP (Standard SKU) | ~$3.65/mo | $0.005/hr, always billed; Basic SKU retired Sep 2025 |
| Egress bandwidth | First 100 GB/mo free | Then ~$0.09/GB; a test server stays well under the free tier |

**Recommendation:** start with **D2s v6** and resize up only for load-test sessions (resize is a Portal operation with a brief reboot; disk, IP, and data are untouched). Stick to one series — Dsv6 is Intel-only, and while determinism is designed to hold across CPU vendors, pinning one CPU generation removes the variable entirely.

## 3. Dev/Test offer — Windows at Linux rates

If you hold a Visual Studio subscription, this is the single biggest cost lever: the [Azure Dev/Test offer](https://azure.microsoft.com/en-us/pricing/offers/dev-test) bills Windows VMs at the **Linux rate** — the Windows Server license surcharge disappears — for development and testing workloads. A D2s v6 drops from ~$0.19/hr to ~$0.10/hr.

- Sign up for the **Pay-As-You-Go Dev/Test** subscription (linked to your VS subscription) and create the VM inside it.
- VS subscriptions also include monthly Azure credits ($50–150 depending on tier) — enough to cover light testing outright.
- The offer is restricted to dev/test use; move to a normal subscription before hosting anything public-facing or production.

No VS subscription: new Azure accounts still get **$200 in credits for 30 days** — use them to trial sizes for free.

## 4. Cost scenarios

Totals = compute + ~$13/mo fixed overhead (disk + static IP). Dev/Test column assumes the §3 offer.

| Usage | Size | Windows PAYG /mo | Dev/Test /mo |
| --- | --- | --- | --- |
| Light (2–3 h/day, ~75 h/mo) | D2s v6 | ~$28 | ~$21 |
| | D4s v6 | ~$42 | ~$28 |
| | D8s v6 | ~$71 | ~$43 |
| Moderate (5–6 h/day, ~165 h/mo) | D2s v6 | ~$45 | ~$30 |
| | D4s v6 | ~$77 | ~$46 |
| | D8s v6 | ~$140 | ~$79 |
| Heavy (10+ h/day, ~330 h/mo) | D2s v6 | ~$77 | ~$46 |
| | D4s v6 | ~$141 | ~$79 |
| | D8s v6 | ~$267 | ~$145 |

## 5. Step-by-step setup

### 5.1 Account

1. Create/sign in at [portal.azure.com](https://portal.azure.com). New accounts get $200/30-day credits.
2. VS subscribers: create a Pay-As-You-Go Dev/Test subscription first (§3) and do everything below inside it.

### 5.2 Create the VM

1. Portal → **Create a resource → Virtual Machine**.
2. Basics:
   - Resource group: new, e.g. `gameserver-rg`
   - Name: e.g. `gameserver-test`
   - Region: closest to you/testers (East US is typically cheapest)
   - Image: **Windows Server 2025 Datacenter: Azure Edition**
   - Size: **D2s_v6** (See all sizes → search)
   - Admin username/password: used for RDP
3. Disks: OS disk type **Standard SSD** (Premium is unnecessary for a game server binary).
4. Networking: defaults are fine; a public IP is created automatically (made static in 5.3).
5. Management: enable **Auto-shutdown** at your end-of-day time — this deallocates, so a forgotten VM never bills overnight.
6. Review + Create. Deployment takes 2–5 minutes.

### 5.3 Make the public IP static

New VMs get a Standard-SKU public IP that may still be set to dynamic assignment:

1. VM → **Networking** → click the public IP link → **Configuration**.
2. Assignment: **Static** → Save.

The address now survives deallocate/start cycles (~$3.65/mo, billed 24/7).

### 5.4 Network Security Group rules

Azure blocks all inbound traffic except RDP (TCP 3389) by default. The Broken Engine server needs exactly one game rule:

| Rule | Port | Protocol | Source | Why |
| --- | --- | --- | --- | --- |
| Game traffic | **27015** | **UDP** | Any (or tester IPs) | ENet game port, hard-coded `kuiDefaultPort` |
| RDP lockdown | 3389 | TCP | **Your home IP only** | Never leave RDP open to `0.0.0.0/0` |

Do **not** open:

- **UDP 27016** (LAN discovery) — discovery is subnet broadcast and cannot traverse the WAN; cloud clients connect by explicit address.
- The **agent command port** (`--agent-port`) — it binds loopback only and is unreachable from outside regardless.

Add the game rule: VM → Networking → **Add inbound port rule** → destination port 27015, protocol UDP.

## 6. Deploying the Broken Engine server

### What to copy

The server is fully self-contained — static CRT (no VC++ redistributable) and no Vulkan/GPU dependency. Deploy two things:

1. **The executable** from `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/Output/`:
   - `BrokenEngineSandboxServer.exe` (Release) — normal choice
   - `BrokenEngineSandboxServer.Debug.exe` / `.Profile.exe` — only for remote-debug sessions (§8)
2. **The `Data/` directory** of `.pack` + `.manifest` chunks (Audio, Font, Scene, Islands, Model, Texture, Raw — Shader is client-only). Runtime reads only these; regenerate with DataPacker before deploying if assets changed.

Copy via RDP drag-and-drop, or install OpenSSH Server on the VM for `scp`/robocopy pushes.

### Launch

```
BrokenEngineSandboxServer.exe --data-directory C:\GameServer\Data --log-file C:\GameServer\Logs\server.log
```

- **Omit `--loopback-only`** — without it the server binds all interfaces (required for remote clients). The harness's loopback flag is for local development only.
- The game port is **not configurable**: 27015 always (`kuiDefaultPort`, `Engine/Source/Network/NetworkProtocol.h`).
- `--agent-port <n>` is optional and loopback-only — useful if you RDP in and want harness-style control of the live server.
- The server shows a small "Headless Monitoring Window" (GDI); it is not a console app.
- On quit the server autosaves; on next start it loads that autosave — sim state carries across VM sessions as long as the binary/Data stay compatible.

### Version discipline

The connection handshake validates wire protocol version, deterministic Frame version, and Islands manifest identity; after connect, per-tick CRC comparison disconnects on desync (3 desyncs in 10 s). Practical rule: **deploy client, server, and `Data/` from the same build together** — a stale server or stale packs will refuse clients or desync them. Clients connect by explicit IP (the static IP from 5.3); LAN discovery does not apply over WAN.

## 7. Day-to-day workflow

**Start a session**

1. Portal → Virtual Machines → your VM → **Start** (boots in 30–60 s).
2. RDP in; everything is as you left it. Launch the server.

**End a session**

1. Quit the server (it autosaves).
2. Portal → **Stop** → confirm. State becomes "Stopped (deallocated)"; compute billing ends.

> **Deallocate from the Portal/CLI, not from inside Windows.** Start → Shut Down inside the OS leaves the VM "Stopped" but still *allocated* and billing. Auto-shutdown (5.2) is the safety net.

**Resize** (for load tests): deallocate → VM → **Size** → pick D4s_v6/D8s_v6 → Resize → start. Disk, IP, and data are unchanged. Resize back down afterwards.

## 8. Visual Studio 2026 remote debugging

VS 2026 on your dev machine can attach to the server process on the VM — breakpoints, watches, stepping.

**One-time setup**

1. On the VM, install **Remote Tools for Visual Studio 2026** (must match your VS major version) from Microsoft's download page.
2. Run the remote debugger (`msvsmon.exe`); allow connections on first launch. Default port: **TCP 4026** (a 32-bit debuggee spawns a helper on 4025 — not relevant here, the server is x64-only).
3. NSG inbound rule: TCP 4026, **source restricted to your dev machine's IP**.
4. Optional: register msvsmon as a service/scheduled task so it survives reboots.

**Each session**

1. Deploy and run a **Debug** build (`BrokenEngineSandboxServer.Debug.exe`) with its matching `.pdb` alongside.
2. VS → Debug → **Attach to Process** → connection target `<static-ip>:4026` → authenticate with the VM admin credentials.
3. Select the server process, attach, set breakpoints locally.

Breakpoints bind only if local source and PDBs match the deployed binary exactly — always copy the PDB from the same build as the EXE. Note `/fp:strict` holds in Debug too, so determinism-sensitive behavior reproduces under the debugger.

## 9. Security and patching

- **RDP**: restrict 3389 to your home IP in the NSG; use a long unique admin password. Consider Azure Bastion if your home IP changes often.
- **The server already assumes hostile input**: all inbound client packets pass per-packet size/rate validation with violation counting and disconnect (`Documents/Architecture/Network.md`). Exposing UDP 27015 publicly is within the protocol's threat model, but restrict the NSG source to tester IPs when you know them.
- **Windows Firewall**: mirror the NSG (allow inbound UDP 27015 for the server exe). Watch for the first-run "Windows Security Alert" prompt — clicking Cancel creates a per-executable block rule that silently overrides allows.
- **Updates**: with frequent deallocation, Windows Update queues work between sessions — leave the VM running occasionally to let patches finish, or check manually weekly.
- **Backups**: the deployable is just EXE + `Data/` + autosave, all reproducible from your dev machine and repo. Skip paid Azure Backup; copy the autosave off the VM if a long-running world matters.

## 10. Cost tips and cheat sheet

- **Auto-shutdown is the essential setting.** One forgotten night on a D8s v6 wastes ~$9 (about half that at Dev/Test rates).
- **Dev/Test offer first** (§3) — halves compute if you have any VS subscription.
- **Start D2s v6, resize up only for load tests**, then back down.
- **Standard SSD, not Premium.**
- **Budget alert**: Cost Management + Billing → set a monthly budget (e.g. $50) with email alerts.
- **Savings Plan** only if usage becomes many hours daily — 1-year commitments cut compute 30–40% but don't fit intermittent use.

| Action | How |
| --- | --- |
| Start VM | Portal → VM → Start, or `az vm start -g gameserver-rg -n gameserver-test` |
| Stop (stop billing) | Portal → VM → Stop, or `az vm deallocate -g gameserver-rg -n gameserver-test` |
| Check IP | Portal → VM → Overview → Public IP address |
| Resize | Deallocate → VM → Size → pick → Resize |
| Open a port | VM → Networking → Add inbound port rule |
| RDP | Portal → VM → Connect → RDP → download `.rdp` |
| Remote debug | VS → Debug → Attach to Process → `<ip>:4026` |
| Server launch | `BrokenEngineSandboxServer.exe --data-directory <abs> --log-file <path>` |
