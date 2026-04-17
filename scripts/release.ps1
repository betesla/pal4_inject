param(
    [string]$Version = "",
    [string]$Repository = "betesla/pal4_inject",
    [string]$GiteeRepository = "betesla/pal4_inject",
    [string]$GiteeAccessToken = "",
    [string]$BuildDir = "build",
    [string]$Configuration = "Release",
    [string]$GeneratorPlatform = "Win32",
    [string]$ReleaseNotesPath = "",
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$SkipGitHubRelease,
    [switch]$SkipGiteeRelease,
    [switch]$SkipGitPush,
    [switch]$Draft,
    [switch]$Prerelease
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Get-ProjectVersion {
    param([string]$RepoRoot)

    $cmakePath = Join-Path $RepoRoot "CMakeLists.txt"
    $cmake = Get-Content -LiteralPath $cmakePath -Raw
    $match = [regex]::Match($cmake, "project\s*\(\s*pal4_inject\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)")
    if (-not $match.Success) {
        throw "Could not find project(pal4_inject VERSION x.y.z) in CMakeLists.txt."
    }
    return "v$($match.Groups[1].Value)"
}

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory
    )

    Write-Host ">> $FilePath $($Arguments -join ' ')"
    Push-Location $WorkingDirectory
    try {
        & $FilePath @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Command failed with exit code ${LASTEXITCODE}: $FilePath"
        }
    } finally {
        Pop-Location
    }
}

function Get-GitHubCredentialHeader {
    $credentialInput = "protocol=https`nhost=github.com`n`n"
    $credentialText = $credentialInput | git credential fill
    $credential = @{}
    foreach ($line in $credentialText) {
        $parts = $line -split "=", 2
        if ($parts.Length -eq 2) {
            $credential[$parts[0]] = $parts[1]
        }
    }
    if (-not $credential.ContainsKey("username") -or -not $credential.ContainsKey("password")) {
        throw "No GitHub credential returned by git credential manager. Sign in to GitHub for git first."
    }

    $pair = "{0}:{1}" -f $credential["username"], $credential["password"]
    return "Basic $([Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes($pair)))"
}

function Invoke-GitHubJson {
    param(
        [string]$Method,
        [string]$Uri,
        [hashtable]$Headers,
        [object]$Body = $null
    )

    if ($null -eq $Body) {
        return Invoke-RestMethod -Method $Method -Uri $Uri -Headers $Headers
    }

    $json = $Body | ConvertTo-Json -Depth 8
    return Invoke-RestMethod `
        -Method $Method `
        -Uri $Uri `
        -Headers $Headers `
        -ContentType "application/json; charset=utf-8" `
        -Body $json
}

function Split-RepoName {
    param([string]$Repository)

    $parts = $Repository -split "/", 2
    if ($parts.Length -ne 2 -or -not $parts[0] -or -not $parts[1]) {
        throw "Repository must look like owner/repo. Got: $Repository"
    }
    return @{
        Owner = $parts[0]
        Repo = $parts[1]
    }
}

function Get-GiteeToken {
    param([string]$ExplicitToken)

    if ($ExplicitToken) {
        return $ExplicitToken
    }
    $token = [Environment]::GetEnvironmentVariable("GITEE_TOKEN", "Process")
    if (-not $token) { $token = [Environment]::GetEnvironmentVariable("GITEE_TOKEN", "User") }
    if (-not $token) { $token = [Environment]::GetEnvironmentVariable("GITEE_TOKEN", "Machine") }
    if (-not $token) { $token = [Environment]::GetEnvironmentVariable("GITEE_ACCESS_TOKEN", "Process") }
    if (-not $token) { $token = [Environment]::GetEnvironmentVariable("GITEE_ACCESS_TOKEN", "User") }
    if (-not $token) { $token = [Environment]::GetEnvironmentVariable("GITEE_ACCESS_TOKEN", "Machine") }
    if (-not $token) {
        throw "No Gitee token found. Set GITEE_TOKEN or pass -GiteeAccessToken."
    }
    return $token
}

function ConvertTo-QueryString {
    param([hashtable]$Values)

    $pairs = foreach ($key in $Values.Keys) {
        "{0}={1}" -f [uri]::EscapeDataString($key), [uri]::EscapeDataString([string]$Values[$key])
    }
    return $pairs -join "&"
}

function Invoke-GiteeApi {
    param(
        [string]$Method,
        [string]$Uri,
        [hashtable]$Body = $null,
        [switch]$AllowNotFound
    )

    try {
        if ($null -eq $Body) {
            return Invoke-RestMethod -Method $Method -Uri $Uri
        }
        return Invoke-RestMethod `
            -Method $Method `
            -Uri $Uri `
            -ContentType "application/x-www-form-urlencoded" `
            -Body (ConvertTo-QueryString -Values $Body)
    } catch {
        if ($AllowNotFound -and $_.Exception.Response.StatusCode.value__ -eq 404) {
            return $null
        }
        throw
    }
}

function Get-ReleaseNotes {
    param(
        [string]$Version,
        [string]$ReleaseNotesPath
    )

    if ($ReleaseNotesPath) {
        return Get-Content -LiteralPath $ReleaseNotesPath -Raw
    }

    return @(
        "PAL4 Inject $Version",
        "",
        "Install:",
        "Extract the archive into the PAL4 game install directory so PAL4_inject.exe sits next to PAL4.exe and the pal4_inject folder stays in the same directory."
    ) -join "`n"
}

function Send-GiteeReleaseAsset {
    param(
        [string]$UploadUri,
        [string]$Token,
        [string]$AssetPath
    )

    Add-Type -AssemblyName System.Net.Http
    $client = New-Object System.Net.Http.HttpClient
    $content = New-Object System.Net.Http.MultipartFormDataContent
    $fileStream = [System.IO.File]::OpenRead($AssetPath)
    try {
        $content.Add((New-Object System.Net.Http.StringContent($Token)), "access_token")
        $fileContent = New-Object System.Net.Http.StreamContent($fileStream)
        $fileContent.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse("application/zip")
        $content.Add($fileContent, "file", (Split-Path -Leaf $AssetPath))
        $response = $client.PostAsync($UploadUri, $content).GetAwaiter().GetResult()
        $payload = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        if (-not $response.IsSuccessStatusCode) {
            throw "Gitee asset upload failed: HTTP $([int]$response.StatusCode) $payload"
        }
        if ($payload) {
            return $payload | ConvertFrom-Json
        }
        return $null
    } finally {
        $fileStream.Dispose()
        $content.Dispose()
        $client.Dispose()
    }
}

function Invoke-GitReleaseChecks {
    param(
        [string]$Version,
        [switch]$SkipGitPush
    )

    $trackedStatus = git status --porcelain --untracked-files=no
    if ($trackedStatus) {
        throw "Tracked git changes exist. Commit them before publishing a GitHub Release."
    }

    $branch = git branch --show-current
    if (-not $branch) {
        throw "Git is in detached HEAD state. Check out the release branch before publishing."
    }

    $head = (git rev-parse HEAD).Trim()
    $tagExists = $true
    git rev-parse -q --verify "refs/tags/$Version" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $tagExists = $false
    }

    if ($tagExists) {
        $tagCommit = (git rev-list -n 1 $Version).Trim()
        if ($tagCommit -ne $head) {
            throw "Tag $Version exists but does not point at HEAD. Refusing to publish a mismatched release."
        }
    } else {
        Invoke-Checked -FilePath "git" -Arguments @("tag", "-a", $Version, "-m", "PAL4 Inject $Version") -WorkingDirectory $repoRoot
    }

    if (-not $SkipGitPush) {
        Invoke-Checked -FilePath "git" -Arguments @("push", "origin", $branch) -WorkingDirectory $repoRoot
        Invoke-Checked -FilePath "git" -Arguments @("push", "origin", $Version) -WorkingDirectory $repoRoot
    }
}

function Publish-GitHubRelease {
    param(
        [string]$Repository,
        [string]$Version,
        [string]$AssetPath,
        [string]$ReleaseNotes,
        [bool]$Draft,
        [bool]$Prerelease
    )

    $headers = @{
        Authorization = Get-GitHubCredentialHeader
        Accept = "application/vnd.github+json"
        "X-GitHub-Api-Version" = "2022-11-28"
        "User-Agent" = "PAL4-inject-release-script"
    }

    $release = $null
    try {
        $release = Invoke-GitHubJson `
            -Method Get `
            -Uri "https://api.github.com/repos/$Repository/releases/tags/$Version" `
            -Headers $headers
    } catch {
        if ($_.Exception.Response.StatusCode.value__ -ne 404) {
            throw
        }
    }

    $payload = @{
        name = "PAL4 Inject $Version"
        body = $ReleaseNotes
        draft = $Draft
        prerelease = $Prerelease
    }

    if ($null -eq $release) {
        $payload["tag_name"] = $Version
        $payload["target_commitish"] = "main"
        $release = Invoke-GitHubJson `
            -Method Post `
            -Uri "https://api.github.com/repos/$Repository/releases" `
            -Headers $headers `
            -Body $payload
    } else {
        $release = Invoke-GitHubJson `
            -Method Patch `
            -Uri "https://api.github.com/repos/$Repository/releases/$($release.id)" `
            -Headers $headers `
            -Body $payload
    }

    $assetName = Split-Path -Leaf $AssetPath
    $assets = Invoke-GitHubJson `
        -Method Get `
        -Uri "https://api.github.com/repos/$Repository/releases/$($release.id)/assets" `
        -Headers $headers
    foreach ($asset in $assets) {
        if ($asset.name -eq $assetName) {
            Invoke-RestMethod `
                -Method Delete `
                -Uri "https://api.github.com/repos/$Repository/releases/assets/$($asset.id)" `
                -Headers $headers | Out-Null
        }
    }

    $uploadUri = "https://uploads.github.com/repos/$Repository/releases/$($release.id)/assets?name=$([uri]::EscapeDataString($assetName))"
    $uploaded = Invoke-RestMethod `
        -Method Post `
        -Uri $uploadUri `
        -Headers $headers `
        -ContentType "application/zip" `
        -InFile $AssetPath

    return [PSCustomObject]@{
        ReleaseUrl = $release.html_url
        AssetName = $uploaded.name
        AssetSize = $uploaded.size
        AssetState = $uploaded.state
    }
}

function Publish-GiteeRelease {
    param(
        [string]$Repository,
        [string]$Version,
        [string]$AssetPath,
        [string]$ReleaseNotes,
        [bool]$Prerelease,
        [string]$AccessToken
    )

    $repo = Split-RepoName -Repository $Repository
    $owner = $repo.Owner
    $name = $repo.Repo
    $token = Get-GiteeToken -ExplicitToken $AccessToken
    $apiBase = "https://gitee.com/api/v5/repos/$owner/$name"

    $release = Invoke-GiteeApi `
        -Method Get `
        -Uri "$apiBase/releases/$([uri]::EscapeDataString($Version))?access_token=$([uri]::EscapeDataString($token))" `
        -AllowNotFound

    $payload = @{
        access_token = $token
        name = "PAL4 Inject $Version"
        body = $ReleaseNotes
        prerelease = if ($Prerelease) { "true" } else { "false" }
    }

    if ($null -eq $release) {
        $payload["tag_name"] = $Version
        $payload["target_commitish"] = "main"
        $release = Invoke-GiteeApi -Method Post -Uri "$apiBase/releases" -Body $payload
    } else {
        $release = Invoke-GiteeApi -Method Patch -Uri "$apiBase/releases/$([uri]::EscapeDataString($Version))" -Body $payload
    }

    if (-not $release.id) {
        throw "Gitee release response did not include a release id."
    }

    $assetName = Split-Path -Leaf $AssetPath
    $attachFilesUri = "$apiBase/releases/$($release.id)/attach_files"
    $attachFiles = Invoke-GiteeApi `
        -Method Get `
        -Uri "$attachFilesUri?access_token=$([uri]::EscapeDataString($token))"
    foreach ($asset in $attachFiles) {
        if ($asset.name -eq $assetName -or $asset.file_name -eq $assetName) {
            Invoke-GiteeApi `
                -Method Delete `
                -Uri "$attachFilesUri/$($asset.id)?access_token=$([uri]::EscapeDataString($token))" | Out-Null
        }
    }

    $uploaded = Send-GiteeReleaseAsset -UploadUri $attachFilesUri -Token $token -AssetPath $AssetPath
    return [PSCustomObject]@{
        ReleaseUrl = "https://gitee.com/$Repository/releases/tag/$Version"
        AssetName = $assetName
        AssetState = if ($uploaded) { "uploaded" } else { "uploaded-empty-response" }
    }
}

$repoRoot = Resolve-RepoRoot
Set-Location $repoRoot

if (-not $Version) {
    $Version = Get-ProjectVersion -RepoRoot $repoRoot
}
if ($Version -notmatch "^v[0-9]+\.[0-9]+\.[0-9]+$") {
    throw "Version must look like v0.1.1. Got: $Version"
}

$buildPath = Join-Path $repoRoot $BuildDir
$distPath = Join-Path $repoRoot "dist"
$payloadPath = Join-Path $distPath "pal4_inject"
$zipPath = Join-Path $repoRoot "PAL4_inject_${Version}_win32.zip"
$gameExeName = "PAL4.exe"
$gameExePath = Join-Path $distPath $gameExeName
$preservedGameExe = $null

if (-not $SkipBuild) {
    Invoke-Checked -FilePath "cmake" -Arguments @("-S", ".", "-B", $BuildDir, "-A", $GeneratorPlatform) -WorkingDirectory $repoRoot
    Invoke-Checked -FilePath "cmake" -Arguments @("--build", $BuildDir, "--config", $Configuration) -WorkingDirectory $repoRoot
}

if (-not $SkipTests) {
    Invoke-Checked -FilePath "ctest" -Arguments @("-C", $Configuration, "--output-on-failure") -WorkingDirectory $buildPath
}

if (Test-Path -LiteralPath $gameExePath) {
    $tempGameExe = Join-Path ([System.IO.Path]::GetTempPath()) ("pal4_inject_release_" + [guid]::NewGuid().ToString("N") + "_PAL4.exe")
    Copy-Item -LiteralPath $gameExePath -Destination $tempGameExe -Force
    $preservedGameExe = $tempGameExe
}

if ((Test-Path -LiteralPath $distPath) -and ((Resolve-Path -LiteralPath $distPath).Path.StartsWith($repoRoot))) {
    Remove-Item -LiteralPath $distPath -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $payloadPath | Out-Null
Copy-Item -Force -LiteralPath (Join-Path $buildPath "$Configuration\PAL4_inject.exe") -Destination (Join-Path $distPath "PAL4_inject.exe")
Copy-Item -Force -LiteralPath (Join-Path $buildPath "$Configuration\runtime.dll") -Destination (Join-Path $payloadPath "runtime.dll")
Copy-Item -Force -LiteralPath (Join-Path $buildPath "$Configuration\cli.exe") -Destination (Join-Path $payloadPath "cli.exe")
if ($preservedGameExe) {
    Copy-Item -LiteralPath $preservedGameExe -Destination $gameExePath -Force
    Remove-Item -LiteralPath $preservedGameExe -Force
} else {
    Write-Warning "dist\PAL4.exe was not found, so the release zip will not include PAL4.exe. Copy PAL4.exe into dist before publishing if it should be bundled."
}

if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $distPath "*") -DestinationPath $zipPath -Force

$zipListing = tar -tf $zipPath
Write-Host "Created $zipPath"
$zipListing | ForEach-Object { Write-Host "  $_" }

$resolvedGiteeToken = ""
if (-not $SkipGiteeRelease) {
    $resolvedGiteeToken = Get-GiteeToken -ExplicitToken $GiteeAccessToken
}

if (-not $SkipGitHubRelease) {
    Invoke-GitReleaseChecks -Version $Version -SkipGitPush:$SkipGitPush
    $notes = Get-ReleaseNotes -Version $Version -ReleaseNotesPath $ReleaseNotesPath
    $result = Publish-GitHubRelease `
        -Repository $Repository `
        -Version $Version `
        -AssetPath $zipPath `
        -ReleaseNotes $notes `
        -Draft $Draft.IsPresent `
        -Prerelease $Prerelease.IsPresent
    $result
}

if (-not $SkipGiteeRelease) {
    Invoke-GitReleaseChecks -Version $Version -SkipGitPush:$SkipGitPush
    $notes = Get-ReleaseNotes -Version $Version -ReleaseNotesPath $ReleaseNotesPath
    $result = Publish-GiteeRelease `
        -Repository $GiteeRepository `
        -Version $Version `
        -AssetPath $zipPath `
        -ReleaseNotes $notes `
        -Prerelease $Prerelease.IsPresent `
        -AccessToken $resolvedGiteeToken
    $result
}
