# Build the existing TC264 project with the official AURIX Development Studio.
# The ADS bundled TASKING license requires an IDE build; invoking cctc directly
# is not supported. A private headless staging project avoids the UI-only
# Project Booster builder, and regenerates makefiles with current local paths.
[CmdletBinding()]
param(
    [string]$StudioRoot = '',
    [switch]$Clean,
    [int]$BuildTimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$focProjectRoot = $PSScriptRoot
$focBuildRoot = Join-Path $focProjectRoot 'build'
$focStageRoot = Join-Path $focBuildRoot 'ads-project'
$focWorkspace = Join-Path $focBuildRoot 'ads-workspace'
$focLog = Join-Path $focBuildRoot 'build.log'

if (!$StudioRoot) {
    $focInstallBases = @('D:\Infineon', 'C:\Infineon', 'C:\Program Files\Infineon')
    $focCandidates = foreach ($focInstallBase in $focInstallBases) {
        if (Test-Path -LiteralPath $focInstallBase) {
            Get-ChildItem -LiteralPath $focInstallBase -Directory -Filter 'AURIX-Studio-*'
        }
    }
    $focStudio = $focCandidates | Sort-Object Name -Descending |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'AURIX-studioc.exe') } |
        Select-Object -First 1
    if (!$focStudio) {
        throw 'AURIX Development Studio was not found. Pass -StudioRoot with its installation directory.'
    }
    $StudioRoot = $focStudio.FullName
}
$StudioRoot = (Resolve-Path -LiteralPath $StudioRoot).Path
$focLauncher = Join-Path $StudioRoot 'AURIX-studioc.exe'
$focOriginalIni = Join-Path $StudioRoot 'AURIX-studio.ini'
if (!(Test-Path -LiteralPath $focLauncher) -or !(Test-Path -LiteralPath $focOriginalIni)) {
    throw 'The selected installation does not contain AURIX-studioc.exe and AURIX-studio.ini.'
}

New-Item -ItemType Directory -Force -Path $focBuildRoot, $focStageRoot | Out-Null
foreach ($focDirectory in @($focBuildRoot, $focStageRoot)) {
    if ((Get-Item -LiteralPath $focDirectory).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Build directories cannot be junctions or symbolic links: $focDirectory"
    }
}

# Refresh only our source snapshot. Keep its generated Debug directory so an
# ordinary repeat build remains incremental. Removed source files disappear.
foreach ($focSourceDirectory in @('code', 'libraries', 'user', '.settings')) {
    $focSource = Join-Path $focProjectRoot $focSourceDirectory
    if (!(Test-Path -LiteralPath $focSource)) { throw "Missing project directory: $focSource" }
    $focTarget = [IO.Path]::GetFullPath((Join-Path $focStageRoot $focSourceDirectory))
    $focStagePrefix = [IO.Path]::GetFullPath($focStageRoot).TrimEnd('\') + '\'
    if (!$focTarget.StartsWith($focStagePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to refresh a path outside the staging project: $focTarget"
    }
    if (Test-Path -LiteralPath $focTarget) {
        Remove-Item -LiteralPath $focTarget -Recurse -Force
    }
    Copy-Item -LiteralPath $focSource -Destination $focStageRoot -Recurse -Force
}
foreach ($focSourceFile in @('.project', '.cproject', 'Lcf_Tasking_Tricore_Tc.lsl')) {
    Copy-Item -LiteralPath (Join-Path $focProjectRoot $focSourceFile) -Destination $focStageRoot -Force
}
foreach ($focOptionalFile in @('makefile.init', 'makefile.defs', 'makefile.targets')) {
    $focOptionalSource = Join-Path $focProjectRoot $focOptionalFile
    if (Test-Path -LiteralPath $focOptionalSource) {
        Copy-Item -LiteralPath $focOptionalSource -Destination $focStageRoot -Force
    }
}

# Standard CDT builds all configured sources and links with the original LSL.
# These two Infineon convenience builders require a graphical Workbench.
[xml]$focDescription = Get-Content -LiteralPath (Join-Path $focStageRoot '.project') -Raw
foreach ($focBuilder in @($focDescription.projectDescription.buildSpec.buildCommand)) {
    if ($focBuilder.name -in @('com.infineon.aurix.buildsystem.builders.booster',
                              'com.infineon.aurix.buildsystem.builders.autodiscovery')) {
        $null = $focDescription.projectDescription.buildSpec.RemoveChild($focBuilder)
    }
}
$focDescription.Save((Join-Path $focStageRoot '.project'))
$focProjectName = [string]$focDescription.projectDescription.name

# ADS' normal launcher ini contains -perspective, which the official CDT
# headless application cannot parse. Preserve the rest of the installed ini.
$focIniLines = Get-Content -LiteralPath $focOriginalIni
$focHeadlessLines = for ($focLine = 0; $focLine -lt $focIniLines.Count; $focLine++) {
    if ($focIniLines[$focLine] -eq '-perspective') { $focLine++; continue }
    if ($focIniLines[$focLine].StartsWith('plugins/')) {
        Join-Path $StudioRoot $focIniLines[$focLine]
    } else {
        $focIniLines[$focLine]
    }
}
$focHeadlessIni = Join-Path $focBuildRoot 'ads-headless.ini'
$focHeadlessLines | Set-Content -LiteralPath $focHeadlessIni -Encoding ascii

$focBuildArgument = if ($Clean) { '-cleanBuild' } else { '-build' }
$focArguments = @('--launcher.ini', $focHeadlessIni, '-nosplash', '-consoleLog',
    '-application', 'org.eclipse.cdt.managedbuilder.core.headlessbuild',
    '-data', $focWorkspace, '-import', $focStageRoot, $focBuildArgument,
    ($focProjectName + '/Debug'), '-no-indexer', '-printErrorMarkers')

Write-Host "Building TC264 with $StudioRoot"
$focErrorLog = Join-Path $focBuildRoot 'build.stderr.log'
$focQuotedArguments = $focArguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }
$focProcess = Start-Process -FilePath $focLauncher -ArgumentList ($focQuotedArguments -join ' ') `
    -WindowStyle Hidden -RedirectStandardOutput $focLog -RedirectStandardError $focErrorLog -PassThru
$focDeadline = [DateTime]::UtcNow.AddSeconds($BuildTimeoutSeconds)
$focCompletionTime = $null
while (!$focProcess.HasExited) {
    $focProgress = if (Test-Path -LiteralPath $focLog) { Get-Content -LiteralPath $focLog -Raw } else { '' }
    if (!$focCompletionTime -and ($focProgress -match 'Headless build (completed successfully|encountered errors)')) {
        $focCompletionTime = [DateTime]::UtcNow
    }
    # Some ADS background services keep the launcher alive after the official
    # headless application has reported its final result. Only close the exact
    # helper we launched, after allowing the completed workspace to flush.
    if ($focCompletionTime -and [DateTime]::UtcNow -gt $focCompletionTime.AddSeconds(15)) {
        Stop-Process -Id $focProcess.Id
        break
    }
    if ([DateTime]::UtcNow -gt $focDeadline) {
        Stop-Process -Id $focProcess.Id
        throw "TC264 build timed out after $BuildTimeoutSeconds seconds. Read $focLog"
    }
    Start-Sleep -Milliseconds 500
    $focProcess.Refresh()
}
$focProcess.WaitForExit()
$focBuildText = Get-Content -LiteralPath $focLog -Raw
Write-Host $focBuildText
if ((Get-Item -LiteralPath $focErrorLog).Length -gt 0) {
    Write-Host (Get-Content -LiteralPath $focErrorLog -Raw)
}
if (!$focBuildText.Contains('Headless build completed successfully')) {
    throw "TC264 build failed. Read $focLog and $focErrorLog"
}

foreach ($focExtension in @('elf', 'hex', 'map')) {
    $focArtifact = Join-Path $focStageRoot ('Debug\' + $focProjectName + '.' + $focExtension)
    if (!(Test-Path -LiteralPath $focArtifact)) { throw "Missing linked output: $focArtifact" }
    Copy-Item -LiteralPath $focArtifact -Destination (Join-Path $focBuildRoot ('FOC_TC264.' + $focExtension)) -Force
}
Write-Host "TC264 ELF/HEX/MAP ready in $focBuildRoot"
