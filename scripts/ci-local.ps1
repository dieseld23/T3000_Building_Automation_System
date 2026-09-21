<#
.SYNOPSIS
    Runs the GitHub "Testing the MSBuild" check locally, against a clean checkout.

.DESCRIPTION
    .github/workflows/BuildTest.yml spends about half of its ~9 minutes installing
    MFC for the v143 toolset and the .NET Framework 4.5.2 reference assemblies onto
    a fresh runner image. Both are already installed on this machine, so locally
    only the build itself is left to do.

    What CI still adds is the CLEAN CHECKOUT: it builds what is committed, so a
    file that was never "git add"ed fails there and not in your working tree. That
    is the part worth reproducing, and it is what this script does - it builds a
    throwaway git worktree at a given ref, never the working tree itself.

    Submodules are deliberately NOT initialised. No project in "T3000 - VS2019.sln"
    lives under, or references, T3000_CrossPlatform, PartsAndVendors or
    T3000Webview, so cloning them would cost minutes and change nothing. If that
    ever stops being true this script stops being equivalent to CI, so it re-checks
    the assumption on every run rather than quietly passing.

.EXAMPLE
    pwsh scripts/ci-local.ps1
    pwsh scripts/ci-local.ps1 -Ref origin/master -Parallel
#>
[CmdletBinding()]
param(
    # What to build. Anything git can resolve: HEAD, a branch, a tag, a SHA.
    [string] $Ref = 'HEAD',

    # Short on purpose. MFC plus vcpkg-export-openssl produce deep paths, and this
    # build hits MAX_PATH when rooted somewhere long like a temp directory.
    [string] $WorktreePath = 'C:\t3000-ci',

    # Adds /m. Faster, but then it is no longer the exact command CI runs.
    [switch] $Parallel,

    # Leaves the worktree in place afterwards, to inspect or run the output.
    [switch] $Keep
)

$ErrorActionPreference = 'Stop'

function Step($text) { Write-Host "`n=== $text" -ForegroundColor Cyan }
function Ok($text)   { Write-Host "  ok    $text" -ForegroundColor DarkGreen }
function Note($text) { Write-Host "  note  $text" -ForegroundColor DarkYellow }

$started = Get-Date

$repo = & git rev-parse --show-toplevel 2>$null
if (-not $repo) { throw "Not inside a git repository." }
$repo = $repo.Replace('/', '\')

# -------------------------------------------------------------------- preflight
# The two steps CI runs before building. Checked rather than installed: if one is
# missing the build dies at MSB8041 or MSB3644 with no hint of why, and fixing it
# is a Visual Studio Installer job, not something to do silently from a script.
Step "Preflight (the two things CI installs on its runner)"

$mfcGlob = 'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\14.[34]*\atlmfc\include\afxwin.h'
$mfc = Get-ChildItem $mfcGlob -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $mfc) {
    Write-Error "MFC for the v143 toolset is missing. In the Visual Studio Installer add the MFC component for v143 build tools; without it the build stops at MSB8041."
}
Ok "MFC v143 - $($mfc.FullName)"

$refRoot = Join-Path ${env:ProgramFiles(x86)} 'Reference Assemblies\Microsoft\Framework\.NETFramework\v4.5.2'
if (-not (Test-Path (Join-Path $refRoot 'mscorlib.dll'))) {
    Write-Error ".NET Framework 4.5.2 reference assemblies are missing, so the managed projects will stop at MSB3644. CI installs the Microsoft.NETFramework.ReferenceAssemblies.net452 NuGet package into $refRoot - see BuildTest.yml."
}
Ok ".NET 4.5.2 reference assemblies"

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' |
           Select-Object -First 1
if (-not $msbuild) { throw "MSBuild not found via vswhere." }
Ok "MSBuild - $msbuild"

# ----------------------------------------------------------------- what is built
Step "Resolving $Ref"

$sha = & git -C $repo rev-parse --verify "$Ref^{commit}" 2>$null
if (-not $sha) { throw "Cannot resolve '$Ref' to a commit." }
Ok (& git -C $repo log -1 --format='%h %s' $sha)

# The whole point of building a worktree is that uncommitted work is invisible to
# it, exactly as it is invisible to CI. Say what is being left out, so a pass is
# not misread as "my working tree builds".
$dirty = @(& git -C $repo status --porcelain)
if ($dirty.Count -gt 0) {
    Note "$($dirty.Count) uncommitted path(s) are NOT part of this build:"
    $dirty | Select-Object -First 10 | ForEach-Object { Write-Host "          $_" -ForegroundColor DarkYellow }
    if ($dirty.Count -gt 10) { Note "... and $($dirty.Count - 10) more" }
}

# ----------------------------------------------------------------- clean checkout
Step "Clean checkout at $WorktreePath"

if (Test-Path $WorktreePath) {
    & git -C $repo worktree remove --force $WorktreePath 2>$null | Out-Null
    if (Test-Path $WorktreePath) { Remove-Item $WorktreePath -Recurse -Force }
}
& git -C $repo worktree prune
# git writes its checkout progress to stderr, so both streams are muted here -
# 3800 "Updating files: n%" lines bury everything this script has to say.
& git -C $repo worktree add --detach $WorktreePath $sha 2>&1 | Out-Null
if (-not (Test-Path $WorktreePath)) { throw "git worktree add failed." }
Ok "checked out clean, no submodules"

# Re-checks the assumption that lets this script skip "submodules: recursive".
$sln = Join-Path $WorktreePath 'T3000 - VS2019.sln'
$slnText = Get-Content $sln -Raw
foreach ($sub in 'T3000_CrossPlatform', 'PartsAndVendors', 'T3000Webview') {
    if ($slnText.Contains($sub)) {
        Write-Error "The solution now references the $sub submodule, so skipping submodules no longer matches CI. Add 'git submodule update --init --recursive' in the worktree."
    }
}

# ------------------------------------------------------------------------- build
Step "Build"

$msbuildArgs = @(
    $sln
    '/p:Platform=x86'
    '/p:Configuration=Release'
    '/p:ProjectVersion=20230804'
    '/nologo'
    '/v:minimal'
)
if ($Parallel) {
    $msbuildArgs += '/m'
    Note "/m added - faster, but no longer the exact command CI runs"
}

# Not beside the worktree: the default worktree is at the root of C:\, where the
# stock Windows ACL lets Users create directories but not files. And not inside
# the worktree either, since that is deleted before you would go read it.
$log = Join-Path $env:TEMP 't3000-ci.log'
Write-Host "  log   $log"

$buildStarted = Get-Date
& $msbuild @msbuildArgs 2>&1 | Tee-Object -FilePath $log
$code = $LASTEXITCODE
$buildMins = [math]::Round(((Get-Date) - $buildStarted).TotalMinutes, 1)

# ------------------------------------------------------------------------ result
Step "Result"

if ($code -ne 0) {
    $errors = Select-String -Path $log -Pattern 'error [A-Z]+[0-9]+' | Select-Object -First 15
    if ($errors) {
        Write-Host "  First errors:" -ForegroundColor Red
        $errors | ForEach-Object { Write-Host "    $($_.Line.Trim())" -ForegroundColor Red }
    }
    Write-Host "`n  FAILED in $buildMins min (exit $code). Full log: $log" -ForegroundColor Red
}
else {
    $exe = Join-Path $WorktreePath 'T3000 Output\release\T3000.exe'
    if (Test-Path $exe) { Ok "T3000.exe - $([math]::Round((Get-Item $exe).Length / 1MB, 1)) MB" }
    Write-Host "`n  PASSED in $buildMins min" -ForegroundColor Green
}

if ($Keep) {
    Note "worktree kept at $WorktreePath"
    Note "remove it with: git -C `"$repo`" worktree remove --force `"$WorktreePath`""
}
else {
    & git -C $repo worktree remove --force $WorktreePath 2>$null | Out-Null
    if (Test-Path $WorktreePath) { Remove-Item $WorktreePath -Recurse -Force -ErrorAction SilentlyContinue }
    & git -C $repo worktree prune
}

Write-Host "  total $([math]::Round(((Get-Date) - $started).TotalMinutes, 1)) min"
exit $code
