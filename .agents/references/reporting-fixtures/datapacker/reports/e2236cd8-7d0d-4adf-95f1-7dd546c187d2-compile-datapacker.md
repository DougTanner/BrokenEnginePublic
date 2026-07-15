Schema: be-agent-report/v1
Requested role: Sonnet/Luna build agent
Actual executor: Codex subagent (Luna build role)
Fallback: none
Paired-review diversity: N/A
Worktree: <WORKTREE>
Session-start baseline: ca6f005addca80e8273cc7732e436fe351c1f71c
Plan/intent: Provision worktree ThirdParty and synchronously compile DataPacker Release only using current-checkout AgentCli and exact requested MSBuild properties; do not build AgentCli or execute DataPacker.

## Lifecycle validation

- PRIMARY: <USER_HOME>\Documents\BrokenEnginePublic
- ROOT: <WORKTREE>
- BASELINE: ca6f005addca80e8273cc7732e436fe351c1f71c
- PRIMARY and ROOT: distinct canonical directories
- Git common directory: <USER_HOME>\Documents\BrokenEnginePublic\.git
- Live wrapper AgentCli session claim: present
- ReportPath containment and initial absence: passed

## Provisioning

Status: success

Output:

```text
Shared worktree dependencies validated for '<WORKTREE>' using primary '<USER_HOME>\Documents\BrokenEnginePublic'.
```

AgentCli executable:

```text
<WORKTREE>\Tools\AgentCli\Platforms\VisualStudio2026\Output\AgentCli.exe
```

## Build

Project: DataPacker
Configuration: Release
Platform: x64
Execution: synchronous foreground
Final status: fail
Final AgentCli exit status: 1

Command:

```text
AgentCli.exe build <WORKTREE>\DataPacker\Platforms\VisualStudio2026\DataPacker.sln /p:Configuration=Release /p:Platform=x64 /p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false /verbosity:minimal
```

Every error line verbatim:

```text
  <WORKTREE>\.agents\scripts\Provision-Workt
  reeThirdParty.ps1 : Method invocation failed because [System.Security.Cryptography.SHA256] does not contain a method
  named 'HashData'.
      + CategoryInfo          : InvalidOperation: (:) [Provision-WorktreeThirdParty.ps1], RuntimeException
      + FullyQualifiedErrorId : MethodNotFound,Provision-WorktreeThirdParty.ps1

C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: The command "powershell.exe -NoProfile -ExecutionPolicy Bypass -File "<WORKTREE>\.agents\scripts\Provision-WorktreeThirdParty.ps1" -RepositoryRoot "<WORKTREE>" [<WORKTREE>\DataPacker\Platforms\VisualStudio2026\DataPacker.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: if errorlevel 1 exit /b %errorlevel% [<WORKTREE>\DataPacker\Platforms\VisualStudio2026\DataPacker.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: if not EXIST "<WORKTREE>\ThirdParty\Prebuilts\Platforms\VisualStudio2026\Output\ThirdParty.Release.lib" ( [<WORKTREE>\DataPacker\Platforms\VisualStudio2026\DataPacker.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073:     echo Required ThirdParty.Release.lib is missing after provisioning. 1>&2 [<WORKTREE>\DataPacker\Platforms\VisualStudio2026\DataPacker.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073:     exit /b 1 [<WORKTREE>\DataPacker\Platforms\VisualStudio2026\DataPacker.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: ) [<WORKTREE>\DataPacker\Platforms\VisualStudio2026\DataPacker.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: :VCEnd" exited with code 1. [<WORKTREE>\DataPacker\Platforms\VisualStudio2026\DataPacker.vcxproj]
```

Relevant warning lines: none

## Result

The required DataPacker Release build failed before compilation because the MSBuild project provisioning hook invoked Windows PowerShell, whose runtime does not provide `System.Security.Cryptography.SHA256.HashData` used by `Provision-WorktreeThirdParty.ps1`.

Files changed: none
Functions/regions touched: none
Residuals:
- R001: Required DataPacker Release build failed with AgentCli exit status 1; provisioning hook is incompatible with the Windows PowerShell runtime launched by MSBuild.
