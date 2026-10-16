package main

// GitHub Releases: the update channel. "release" follows the newest full
// release; "prerelease" follows the newest release of any kind, so testers
// get pre-release builds as soon as they are published.

import (
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

const (
	ChannelRelease    = "release"
	ChannelPrerelease = "prerelease"
)

type ReleaseInfo struct {
	Tag        string `json:"tag"`
	Name       string `json:"name"`
	Notes      string `json:"notes"`
	Prerelease bool   `json:"prerelease"`
	Published  string `json:"published"`
	AssetName  string `json:"asset_name"`
	AssetURL   string `json:"asset_url"`
	AssetSize  int64  `json:"asset_size"`
	PageURL    string `json:"page_url"`
}

var httpClient = &http.Client{Timeout: 30 * time.Second}

// githubAPI is the REST endpoint (tests point it at a local server).
var githubAPI = "https://api.github.com"
var githubUploads = "https://uploads.github.com"

type ghRelease struct {
	TagName     string `json:"tag_name"`
	Name        string `json:"name"`
	Body        string `json:"body"`
	HTMLURL     string `json:"html_url"`
	Draft       bool   `json:"draft"`
	Prerelease  bool   `json:"prerelease"`
	PublishedAt string `json:"published_at"`
	Assets      []struct {
		Name string `json:"name"`
		URL  string `json:"browser_download_url"`
		Size int64  `json:"size"`
	} `json:"assets"`
}

// ListReleases returns the repo's published releases that carry a patch zip,
// newest first, filtered by channel.
func ListReleases(repo, channel string) ([]ReleaseInfo, error) {
	req, _ := http.NewRequest("GET", githubAPI+"/repos/"+repo+"/releases?per_page=30", nil)
	req.Header.Set("Accept", "application/vnd.github+json")
	req.Header.Set("User-Agent", "L4D2-PC-Port-Launcher/"+LauncherVersion)
	resp, err := httpClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("cannot reach GitHub (offline?): %v", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode == 404 {
		return nil, fmt.Errorf("github.com/%s was not found", repo)
	}
	if resp.StatusCode == 403 {
		return nil, fmt.Errorf("GitHub rate limit reached - try again in a few minutes, or install from a patch file")
	}
	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("GitHub returned %s", resp.Status)
	}
	var rels []ghRelease
	if err := json.NewDecoder(resp.Body).Decode(&rels); err != nil {
		return nil, err
	}
	var out []ReleaseInfo
	for _, r := range rels {
		if r.Draft || (channel != ChannelPrerelease && r.Prerelease) {
			continue
		}
		info := ReleaseInfo{Tag: r.TagName, Name: r.Name, Notes: r.Body, Prerelease: r.Prerelease,
			Published: r.PublishedAt, PageURL: r.HTMLURL}
		for _, a := range r.Assets {
			if patchAssetRe.MatchString(a.Name) {
				info.AssetName, info.AssetURL, info.AssetSize = a.Name, a.URL, a.Size
				break
			}
		}
		if info.AssetURL != "" {
			out = append(out, info)
		}
	}
	// Newest version first (GitHub orders by creation date; versions are what matter).
	for i := 1; i < len(out); i++ {
		for j := i; j > 0 && CompareVersions(out[j].Tag, out[j-1].Tag) > 0; j-- {
			out[j], out[j-1] = out[j-1], out[j]
		}
	}
	return out, nil
}

// LatestRelease returns the newest release with a patch zip on the channel.
func LatestRelease(repo, channel string) (*ReleaseInfo, error) {
	rels, err := ListReleases(repo, channel)
	if err != nil {
		return nil, err
	}
	if len(rels) == 0 {
		what := "release"
		if channel == ChannelPrerelease {
			what = "release or pre-release"
		}
		return nil, fmt.Errorf("no %s with a patch has been published on github.com/%s yet - install from a local patch file instead", what, repo)
	}
	return &rels[0], nil
}
