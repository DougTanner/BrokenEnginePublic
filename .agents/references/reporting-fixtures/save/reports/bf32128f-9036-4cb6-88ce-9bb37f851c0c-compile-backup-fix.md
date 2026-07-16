Schema: be-agent-report/v1
Requested role: Sonnet/Luna build role
Actual executor: Codex compile subagent
Fallback: none
Paired-review diversity: N/A
Worktree: <WORKTREE>
Session-start baseline: ca6f005addca80e8273cc7732e436fe351c1f71c
Plan/intent: Compile BrokenEngineSandbox Debug client and server after the Engine/Source/File/FileManager.cpp backup-removal fix; build/provision only and do not edit tracked source.

# Overall status

FAIL. Both required WorktreeCli builds returned exit code 1 in the project provisioning pre-build step before C++ compilation. Direct worktree provisioning under PowerShell 7 succeeded before the builds, but each vcxproj invokes `powershell.exe`; that runtime rejected `[System.Security.Cryptography.SHA256]::HashData` in `.agents/scripts/Provision-WorktreeThirdParty.ps1`.

# Lifecycle validation

- Primary checkout: `<USER_HOME>\Documents\BrokenEnginePublic`
- Session worktree: `<WORKTREE>`
- Paths are canonical and distinct: yes
- Shared Git common directory: `<USER_HOME>/Documents/BrokenEnginePublic/.git`
- Wrapper WorktreeCli session owner: `<GUID>`
- WorktreeCli executable: `<WORKTREE>\Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe`
- Pre-build provisioning: success (`Shared worktree dependencies validated`)

# Data mode

- DataBuildMode: `Shared`
- RunDataPacker: `false`
- GameDataDirectory: `<USER_HOME>\Documents\BrokenEnginePublic\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output\Data`
- GeneratedDataIncludeRoot: `<USER_HOME>\Documents\BrokenEnginePublic\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output`
- Mode-selection trigger: baseline diff and untracked-file scan contained no paths under `DataPacker/**`, `Engine/Data/**`, `Projects/BrokenEngineSandbox/Data/**`, `Common/DataFile.h`, generated-header logic, exporter versions/fingerprints, compression, chunk layout, or pack/manifest contracts. The changed engine/game C++ and documentation paths therefore use ordinary Shared mode.
- Required selected-data files: all 26 present and nonempty before compilation.
- Identity comparison: unchanged after client build attempt and unchanged after server build attempt.

## Selected-data identity snapshot

Format: `relative path | length | SHA-256`

```text
Audio.h | 2660 | C6C02393A39FF71096A291A61724F994154DB092DE32A830C2CD056F492F91A2
Audio.manifest | 560 | 880FD054214F22B3D20A02573CEBCB2207E0A9CEC2D0DA917462530A73B05FA6
Audio.pack | 193192368 | 4BFA85CFB07471C630F5EF8F491CF9AE3C6C39983B3AB8DD671408FDA819F1F1
Data.h | 234 | 73CB1934E946C88F57AA267B9BA0EF12E705D2B43BC5B8256ECDA92CA3253874
DataTypes.h | 414 | 57873D4DC9DE8F049EDBC2CE32173E71D62A20C8045219C375B25B3DB173D082
Font.h | 508 | DCA0E774108CAEA4B29C2C62E606FFBFB3188966EE15C83EDAF744BA63AF112F
Font.manifest | 80 | 1BE138F92CC6574F7DAC2A55F3D25883C43739C9E490FEA102A117DC642A8E24
Font.pack | 381824 | 89C2F0BE95BEAB87FDB71881F044E2579E139E80BD1D5FD539138A9F21C7316D
Islands.h | 5607 | EB57E84D93313DE7E515E1417ADBCD0E8F8122E42E3F6F658FECAE291C5D4714
Islands.manifest | 1712 | 611326A2E0F1AA041D2552848392E278012BF446FC4186CB0B6364FB0BCA616B
Islands.pack | 220585696 | A2832FBF11EF64F028B6E40A69EA75125E866FC0C79D7E561A8E0F903FE7BE76
Model.h | 721 | F4D4639EC9C6CF9EDBF97E6117ACF9A91BE2A7C8AE1B2B5BAE25F9EEAC24345A
Model.manifest | 128 | D49A5DB9015AB95E9BE39C52E2525616892723AD65240AB54058E6B31A912C21
Model.pack | 8788560 | D65997C738FEFAC92A48B9E73E784B97D78D098DCAE347F35AE8349513726B3C
Raw.h | 646 | E0D79CD87E76C845E8A81E6B05AFC59B67237A6E29F255626DBC7E6442617811
Raw.manifest | 128 | 86E16E53D80D86AD6835614DE7F8B633F702D18CE466F65DD8093A96ED2BE8EF
Raw.pack | 8602976 | 4BF97CF6117B355A5594615E6C1C1593B38CE4CA24B1ADADDFB91EC5F7799217
Scene.h | 700 | 577FFC9D5E968F802EC1C9975A01C6FB557A38B876E718CA4E573EF806AD352A
Scene.manifest | 128 | 70803A2A8FE05269EFF9E031ED67E4EF93FF5836ECBC53CD3C6FBA7A8973D63D
Scene.pack | 3072 | 4DCEDA28A56FBBAD47B249FC73FE8346ACFD608A334B190316C3F7EBAE278163
Shader.h | 5743 | D87B01AE1B39951565ABE1360E742527D7527B27F1418E7DCD04B7E79B4B89F4
Shader.manifest | 1448 | 9E0C2B885D95D98F809EB00BAA8D7479CEAB513968109308C8B38B689B441DD1
Shader.pack | 648864 | 5CA93DD29A06BB1E2D783B16B6165A2683A6A2209D7E42DA3DE40280128EEA8D
Texture.h | 45022 | 170838C39ABCC823E5A6562F91661F20E0C96DAE249C677B1E89D9D6A189801F
Texture.manifest | 11120 | 0EE08EBAC0810291618785E802F245CE3CB1930F4E749D30FD42F4533AE4F5E8
Texture.pack | 1018005952 | 99E220274235A4681F5293650033A1CE5187F0DA9B8DBD9BAD99EC8927DCFFF0
```

# Build results

## BrokenEngineSandbox client Debug

- Status: fail
- Exit code: 1
- Solution: `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.sln`
- Invocation: serialized foreground `WorktreeCli build`, Debug|x64, Shared data properties, clang-tidy and code analysis disabled, minimal verbosity.
- Warnings involving changed files: none observed.

Verbatim decisive failure and error lines:

```text
<WORKTREE>\.agents\scripts\Provision-WorktreeThirdParty.ps1 : Method invocation failed because [System.Security.Cryptography.SHA256] does not contain a method
named 'HashData'.
    + CategoryInfo          : InvalidOperation: (:) [Provision-WorktreeThirdParty.ps1], RuntimeException
    + FullyQualifiedErrorId : MethodNotFound,Provision-WorktreeThirdParty.ps1
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: The command "powershell.exe -NoProfile -ExecutionPolicy Bypass -File "<WORKTREE>\.agents\scripts\Provision-WorktreeThirdParty.ps1" -RepositoryRoot "<WORKTREE>" [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: if errorlevel 1 exit /b %errorlevel% [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: if not EXIST "<WORKTREE>\ThirdParty\Prebuilts\Platforms\VisualStudio2026\Output\ThirdParty.Debug.lib" ( echo Required ThirdParty.Debug.lib is missing after provisioning. 1>&2 & exit /b 1 ) [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: if /I "Shared"=="Local" if /I "false"=="true" if /I "True"=="True" ( [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073:     "*Undefined*devenv" "<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\..\..\..\..\DataPacker\Platforms\VisualStudio2026\DataPacker.sln" /Build "Release|x64" /project "<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\..\..\..\..\DataPacker\Platforms\VisualStudio2026\DataPacker.vcxproj" [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: ) [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: if /I "Shared"=="Local" if /I "false"=="true" if /I "True"=="False" if not EXIST "<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\..\..\..\..\DataPacker\Platforms\VisualStudio2026\Output\DataPacker.exe" ( [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073:     "*Undefined*devenv" "<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\..\..\..\..\DataPacker\Platforms\VisualStudio2026\DataPacker.sln" /Build "Release|x64" /project "<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\..\..\..\..\DataPacker\Platforms\VisualStudio2026\DataPacker.vcxproj" [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: ) [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073:  [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: :VCEnd" exited with code 1. [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.vcxproj]
```

## BrokenEngineSandbox server Debug

- Status: fail
- Exit code: 1
- Solution: `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandboxServer.sln`
- Invocation: serialized foreground `WorktreeCli build`, Debug|x64, Shared data properties, clang-tidy and code analysis disabled, minimal verbosity.
- Warnings involving changed files: none observed.

Verbatim decisive failure and error lines:

```text
<WORKTREE>\.agents\scripts\Provision-WorktreeThirdParty.ps1 : Method invocation failed because [System.Security.Cryptography.SHA256] does not contain a method
named 'HashData'.
    + CategoryInfo          : InvalidOperation: (:) [Provision-WorktreeThirdParty.ps1], RuntimeException
    + FullyQualifiedErrorId : MethodNotFound,Provision-WorktreeThirdParty.ps1
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: The command "powershell.exe -NoProfile -ExecutionPolicy Bypass -File "<WORKTREE>\.agents\scripts\Provision-WorktreeThirdParty.ps1" -RepositoryRoot "<WORKTREE>" [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: if errorlevel 1 exit /b %errorlevel% [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: if not EXIST "<WORKTREE>\ThirdParty\Prebuilts\Platforms\VisualStudio2026\Output\ThirdParty.Debug.lib" ( echo Required ThirdParty.Debug.lib is missing after provisioning. 1>&2 & exit /b 1 ) [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: if /I "Shared"=="Local" if /I "false"=="true" if /I "True"=="True" ( [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073:     "*Undefined*devenv" "<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\..\..\..\..\DataPacker\Platforms\VisualStudio2026\DataPacker.sln" /Build "Release|x64" /project "<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\..\..\..\..\DataPacker\Platforms\VisualStudio2026\DataPacker.vcxproj" [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: ) [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: if /I "Shared"=="Local" if /I "false"=="true" if /I "True"=="False" if not EXIST "<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\..\..\..\..\DataPacker\Platforms\VisualStudio2026\Output\DataPacker.exe" ( [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073:     "*Undefined*devenv" "<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\..\..\..\..\DataPacker\Platforms\VisualStudio2026\DataPacker.sln" /Build "Release|x64" /project "<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\..\..\..\..\DataPacker\Platforms\VisualStudio2026\DataPacker.vcxproj" [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: ) [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073:  [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.vcxproj]
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Microsoft.CppCommon.targets(156,5): error MSB3073: :VCEnd" exited with code 1. [<WORKTREE>\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.vcxproj]
```

# Residuals

- R001: BrokenEngineSandbox client Debug build failed before C++ compilation because the vcxproj provisioning command ran `.agents/scripts/Provision-WorktreeThirdParty.ps1` under Windows PowerShell, where `SHA256.HashData` is unavailable.
- R002: BrokenEngineSandbox server Debug build failed before C++ compilation for the same provisioning-runtime incompatibility.

Files changed: none
Functions/regions touched: none
Residuals:
- Client Debug build failed before C++ compilation due provisioning-runtime incompatibility.
- Server Debug build failed before C++ compilation due provisioning-runtime incompatibility.
