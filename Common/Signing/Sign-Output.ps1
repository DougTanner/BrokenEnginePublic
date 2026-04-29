<#
.SYNOPSIS
    Smart App Control workaround: signs a built .exe with a self-signed local dev cert.

.DESCRIPTION
    Invoked from the PostBuildEvent of BrokenEngineSandbox.vcxproj,
    BrokenEngineSandboxServer.vcxproj, and DataPacker.vcxproj.

    Windows 11 Smart App Control (SAC) blocks unsigned binaries with no ISG
    reputation. This script signs the output exe with a self-signed
    code-signing certificate so SAC will allow it to run.

    On the first build after clone the script asks the developer (Yes/No
    MessageBox) whether to enable signing. The choice is persisted at:
        %LOCALAPPDATA%\BrokenEngine\signing-consent.txt
    containing the literal text "granted" or "declined".

    To re-prompt, delete that file. To flip the choice, edit the file.

    On a "granted" first run, the script also creates the dev certificate
    (CN=BrokenEngine Local Dev) and installs it into the current user's
    TrustedPublisher and Root certificate stores. Windows shows its own
    one-time security warning when adding to Root; click Yes there.

    Runtime tolerances:
    - If the .exe is locked (e.g., game still running), the script logs a
      warning and exits 0 - matching the project ethos that LNK errors on
      running binaries are non-fatal. The build does not fail.
    - If timestamp.digicert.com is unreachable, the script falls back to
      a no-timestamp signature with a warning. Such signatures lose
      validity when the cert expires (~10 years).
    - In CI / non-interactive sessions, the script exits 0 silently.
      Detection: [Environment]::UserInteractive is false, OR any of
      $env:CI / $env:GITHUB_ACTIONS / $env:TF_BUILD / $env:BUILD_BUILDID
      is set.

.PARAMETER BinaryPath
    Absolute path of the .exe to sign. Provided by MSBuild as $(TargetPath).
#>

param(
    [Parameter(Mandatory = $true)]
    [string]$BinaryPath
)

$ErrorActionPreference = 'Stop'

$script:ConsentDir  = Join-Path $env:LOCALAPPDATA 'BrokenEngine'
$script:ConsentFile = Join-Path $script:ConsentDir 'signing-consent.txt'
$script:CertSubject = 'CN=BrokenEngine Local Dev'

function Test-NonInteractive {
    if (-not [Environment]::UserInteractive) { return $true }
    $ciVars = 'CI', 'GITHUB_ACTIONS', 'TF_BUILD', 'BUILD_BUILDID',
              'JENKINS_URL', 'BUILDKITE', 'TEAMCITY_VERSION',
              'GITLAB_CI', 'CIRCLECI', 'APPVEYOR'
    foreach ($var in $ciVars) {
        if (Test-Path "Env:$var") { return $true }
    }
    try {
        Add-Type -AssemblyName System.Windows.Forms -ErrorAction Stop
    } catch {
        return $true
    }
    return $false
}

function Read-Consent {
    if (-not (Test-Path $script:ConsentFile)) { return $null }
    $raw = Get-Content $script:ConsentFile -Raw
    if (-not $raw) { return $null }
    $value = $raw.Trim().ToLowerInvariant()
    if ($value -eq 'granted' -or $value -eq 'declined') { return $value }
    return $null
}

function Write-Consent([string]$value) {
    if (-not (Test-Path $script:ConsentDir)) {
        New-Item -ItemType Directory -Path $script:ConsentDir -Force | Out-Null
    }
    Set-Content -Path $script:ConsentFile -Value $value -NoNewline
}

function Prompt-Consent {
    Add-Type -AssemblyName System.Windows.Forms
    $body = @'
Windows 11 Smart App Control (SAC) may block this BrokenEngine build from running.

To allow it, the build can sign each .exe with a self-signed local dev certificate (CN=BrokenEngine Local Dev), installed into your TrustedPublisher and Root certificate stores. Windows will pop its own one-time security warning when the cert is added to Root; click Yes there to confirm.

Note: this dialog may appear during a nested DataPacker build invoked by Visual Studio when you build Client or Server. That is normal.

This affects only your account on this machine. No certificate material is written into the repo.

Sign builds with the local dev certificate?

  Yes : enable signing for this and all future builds
  No  : skip signing (build still succeeds; binaries may be blocked by SAC)

Your choice is remembered at:
  %LOCALAPPDATA%\BrokenEngine\signing-consent.txt
Delete that file later to be re-prompted, or edit it to flip the answer.
'@
    $result = [System.Windows.Forms.MessageBox]::Show(
        $body,
        'BrokenEngine - Smart App Control workaround',
        [System.Windows.Forms.MessageBoxButtons]::YesNo,
        [System.Windows.Forms.MessageBoxIcon]::Question)
    if ($result -eq [System.Windows.Forms.DialogResult]::Yes) { return 'granted' }
    return 'declined'
}

function Get-OrCreate-Cert {
    # Find the cert in My, or create it.
    $cert = Get-ChildItem Cert:\CurrentUser\My |
        Where-Object { $_.Subject -eq $script:CertSubject -and $_.HasPrivateKey } |
        Select-Object -First 1

    if (-not $cert) {
        Write-Host "Creating self-signed code-signing certificate: $script:CertSubject"
        $cert = New-SelfSignedCertificate `
            -Subject $script:CertSubject `
            -Type CodeSigningCert `
            -CertStoreLocation Cert:\CurrentUser\My `
            -NotAfter (Get-Date).AddYears(10) `
            -HashAlgorithm SHA256 `
            -KeyUsage DigitalSignature `
            -KeyExportPolicy NonExportable
    }

    # Always verify the cert is present in TrustedPublisher and Root by thumbprint;
    # re-add if missing. A previous run may have created the cert in My but failed
    # to install it in Root (e.g., user clicked No on the OS warning), and SAC's
    # chain check requires both stores. Re-verifying every run prevents silent
    # SAC blocks when My is populated but trust stores are not.
    foreach ($storeName in 'TrustedPublisher', 'Root') {
        $store = New-Object System.Security.Cryptography.X509Certificates.X509Store($storeName, 'CurrentUser')
        $store.Open('ReadWrite')
        try {
            $present = $store.Certificates | Where-Object { $_.Thumbprint -eq $cert.Thumbprint }
            if (-not $present) {
                Write-Host "Installing certificate into CurrentUser\$storeName store..."
                $store.Add($cert)
            }
        } finally {
            $store.Close()
        }
    }
    return $cert
}

# Returns [PSCustomObject] with .Result in:
#   'signed-with-timestamp' | 'signed-without-timestamp' | 'locked' | 'failed'
# .Message carries detail (status text or exception message).
function Try-Sign($cert, [string]$path, [string]$tsServer) {
    $params = @{
        FilePath      = $path
        Certificate   = $cert
        HashAlgorithm = 'SHA256'
        ErrorAction   = 'Stop'
    }
    if ($tsServer) { $params.TimestampServer = $tsServer }

    try {
        $sig = Set-AuthenticodeSignature @params
    } catch [System.IO.IOException] {
        # Narrow lock detection: only treat ERROR_SHARING_VIOLATION (0x80070020)
        # and ERROR_LOCK_VIOLATION (0x80070021) as "exe is running" / "AV holds it".
        # Everything else (disk full, path-too-long, unauthorized) must surface as
        # a real failure rather than silently producing an unsigned binary.
        $h = $_.Exception.HResult
        if ($h -eq -2147024864 -or $h -eq -2147024863) {
            return [PSCustomObject]@{ Result = 'locked'; Message = $_.Exception.Message }
        }
        return [PSCustomObject]@{ Result = 'failed'; Message = "$($_.Exception.GetType().Name): $($_.Exception.Message)" }
    } catch {
        return [PSCustomObject]@{ Result = 'failed'; Message = "$($_.Exception.GetType().Name): $($_.Exception.Message)" }
    }

    if ($null -eq $sig -or $sig.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        $msg = if ($sig -and $sig.StatusMessage) { $sig.StatusMessage }
            elseif ($sig) { $sig.Status.ToString() }
            else { '<no signature returned>' }
        return [PSCustomObject]@{ Result = 'failed'; Message = $msg }
    }

    if ($tsServer) {
        # Verify the timestamp actually embedded. AV scanners can briefly lock the
        # just-signed file; if Get-AuthenticodeSignature throws, trust the original
        # Set-AuthenticodeSignature 'Valid' result rather than fail the build over
        # a transient race.
        try {
            $verify = Get-AuthenticodeSignature -FilePath $path -ErrorAction Stop
        } catch {
            return [PSCustomObject]@{ Result = 'signed-with-timestamp'; Message = "verify skipped: $($_.Exception.Message)" }
        }
        if (-not $verify.TimeStamperCertificate) {
            return [PSCustomObject]@{ Result = 'signed-without-timestamp'; Message = 'timestamp server returned no timestamp' }
        }
        return [PSCustomObject]@{ Result = 'signed-with-timestamp'; Message = '' }
    }
    return [PSCustomObject]@{ Result = 'signed-without-timestamp'; Message = '' }
}

function Sign-Binary($cert, [string]$path) {
    # Skip if the binary is already signed by this exact cert (matched by thumbprint).
    # MSBuild's PostBuildEvent fires on every build because the project's CustomBuildStep
    # uses <Outputs>true</Outputs> (always-stale), so without this early-exit we would
    # rewrite an identical signature, burn a TCP call to the timestamp server, and bump
    # the binary's modify time on every no-op rebuild.
    try {
        $existing = Get-AuthenticodeSignature -FilePath $path -ErrorAction Stop
        if ($existing.Status -eq [System.Management.Automation.SignatureStatus]::Valid -and
            $existing.SignerCertificate -and
            $existing.SignerCertificate.Thumbprint -eq $cert.Thumbprint) {
            return
        }
    } catch {
        # Verify failed (e.g., file briefly locked). Fall through and let Try-Sign handle it.
    }

    Write-Host "Signing $(Split-Path -Leaf $path) with local dev certificate"

    $r1 = Try-Sign $cert $path 'http://timestamp.digicert.com'
    switch ($r1.Result) {
        'signed-with-timestamp' { return }
        'signed-without-timestamp' {
            Write-Host "Signed but no timestamp ($($r1.Message)); signature will become invalid when cert expires."
            return
        }
        'locked' {
            Write-Host "Skipping signing: '$path' is locked (probably running). $($r1.Message)"
            exit 0
        }
    }

    Write-Host "Timestamped sign failed ($($r1.Message)); falling back to no-timestamp signature."
    $r2 = Try-Sign $cert $path $null
    switch ($r2.Result) {
        'signed-without-timestamp' {
            Write-Host "Signed without timestamp; signature will become invalid when cert expires."
            return
        }
        'locked' {
            Write-Host "Skipping signing: '$path' is locked (probably running). $($r2.Message)"
            exit 0
        }
    }
    throw "Set-AuthenticodeSignature failed for '$path'. First attempt (timestamped): $($r1.Message). Fallback (no-timestamp): $($r2.Message)."
}

$consent = Read-Consent
if ($null -eq $consent) {
    if (Test-NonInteractive) { exit 0 }
    $consent = Prompt-Consent
    Write-Consent $consent
}

if ($consent -eq 'declined') { exit 0 }

$cert = Get-OrCreate-Cert
Sign-Binary $cert $BinaryPath
