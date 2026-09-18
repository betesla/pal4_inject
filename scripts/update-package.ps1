# Shared by release.ps1 and the offline updater integration tests.
function Get-UpdateFileHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose(); $stream.Dispose() }
}
function New-UpdatePackage {
    param(
        [Parameter(Mandatory=$true)][string]$Source,
        [Parameter(Mandatory=$true)][string]$Output,
        [Parameter(Mandatory=$true)][string]$Version,
        [string]$PackagePath = "",
        [string]$Notes = ""
    )
    if ($Version -notmatch '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw "Invalid stable update version: $Version"
    }
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    [IO.Directory]::CreateDirectory($Output) | Out-Null
    $package = if ($PackagePath) { $PackagePath } else { Join-Path $Output "PAL4Plus_${Version}_win32.zip" }
    $manifest = Join-Path $Output 'update.json'
    # Manual installation and automatic updates share this exact archive.
    $paths = @('PAL4.exe', 'PAL4Plus.exe', 'pal4_inject/runtime.dll', 'pal4_inject/cli.exe')
    if (Test-Path -LiteralPath (Join-Path $Source 'pal4_inject/THIRD_PARTY_NOTICES.txt')) {
        $paths += 'pal4_inject/THIRD_PARTY_NOTICES.txt'
    }
    $files = @()
    foreach ($relative in $paths) {
        if (-not (Test-Path -LiteralPath (Join-Path $Source $relative) -PathType Leaf)) {
            throw "Required release file missing: $relative"
        }
    }
    $stream = [IO.File]::Open($package, [IO.FileMode]::Create)
    $archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($relative in $paths) {
            $path = Join-Path $Source $relative
            $file = Get-Item -LiteralPath $path -ErrorAction Stop
            $files += [ordered]@{
                path = $relative
                size = $file.Length
                sha256 = Get-UpdateFileHash $path
            }
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $path, $relative) | Out-Null
        }
    } finally {
        $archive.Dispose()
        $stream.Dispose()
    }
    $spec = [ordered]@{
        schema_version = 1
        version = $Version.Substring(1)
        channel = 'stable'
        platform = 'win32'
        notes = $Notes
        package = [ordered]@{
            name = [IO.Path]::GetFileName($package)
            size = (Get-Item -LiteralPath $package).Length
            sha256 = Get-UpdateFileHash $package
        }
        files = $files
        remove = @('PAL4_inject.exe')
    }
    [IO.File]::WriteAllText($manifest, ($spec | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    return @($package, $manifest)
}
