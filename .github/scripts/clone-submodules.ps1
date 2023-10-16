[CmdletBinding()]
param (
    [Parameter(Mandatory=$true)][string]$token,
    [Parameter(Mandatory=$true)][string]$jsParseFilePath,
    [Parameter()][string]$githubRepoContext = "espressif"
)
$gitModules = (node $jsParseFilePath | ConvertFrom-Json -AsHashtable)
$modules = @()

foreach ($module in $gitModules.GetEnumerator()) {
    $modules += @{
        name = $module.Name
        url = $module.Value.url
        path = $module.Value.path
    }
}

foreach ($module in $modules) {
    if ((Test-Path $module.Path) -eq $true) {
        Write-Verbose "Deleting existing path before git clone"
        Remove-Item $module.path -Recurse -Force
    }
    if ($module.url -match '^../') {
        Write-Verbose "Local path found in url: $($module.url), rewriting url"
        $remoteUrl = $module.url.Replace('../','')
    }
    elseif ($module.url -match '^https://github.com') {
        Write-Verbose "github path found in url: $($module.url), rewriting url"
        $remoteUrl = $module.url.Replace('https://github.com/','')
    }
    Write-Verbose "Cloning https://x-access-token:$token@github.com/$($remoteUrl) $($module.path) "
    git clone https://x-access-token:$token@github.com/$remoteUrl $module.path
}