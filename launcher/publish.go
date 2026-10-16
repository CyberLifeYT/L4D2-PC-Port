package main

// Publishing: uploads a patch zip (and optionally the launcher exe) to a
// GitHub Release. Used by port\scripts\release_patch.bat and
// pre_release_patch.bat.
//
// Authentication, first match wins:
//   1. GITHUB_TOKEN or GH_TOKEN environment variable
//   2. `gh auth token` (GitHub CLI, if installed and logged in)
//   3. Git's credential helper for github.com (Git Credential Manager on
//      Windows - the same login `git push` uses; it may open a browser once)

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

type PublishOptions struct {
	Repo       string
	Version    string // tag is "v" + Version
	Notes      string
	Prerelease bool
	Files      []string // assets to upload (patch zip first)
}

func githubToken() (string, string, error) {
	for _, k := range []string{"GITHUB_TOKEN", "GH_TOKEN"} {
		if v := strings.TrimSpace(os.Getenv(k)); v != "" {
			return v, k, nil
		}
	}
	if out, err := hiddenCommand("gh", "auth", "token").Output(); err == nil {
		if t := strings.TrimSpace(string(out)); t != "" {
			return t, "GitHub CLI", nil
		}
	}
	cmd := hiddenCommand("git", "credential", "fill")
	cmd.Stdin = strings.NewReader("protocol=https\nhost=github.com\n\n")
	if out, err := cmd.Output(); err == nil {
		for _, line := range strings.Split(string(out), "\n") {
			if strings.HasPrefix(line, "password=") {
				if t := strings.TrimSpace(strings.TrimPrefix(line, "password=")); t != "" {
					return t, "Git credential manager", nil
				}
			}
		}
	}
	return "", "", errors.New("no GitHub login found. Do one of: set GITHUB_TOKEN to a token with \"Contents: write\" on the repo, run `gh auth login`, or push to the repo once with git so its credential manager has a login")
}

func hiddenCommand(name string, args ...string) *exec.Cmd {
	cmd := exec.Command(name, args...)
	hideWindow(cmd)
	return cmd
}

func ghAPI(token, method, u string, body any, out any) (int, error) {
	var rd io.Reader
	if body != nil {
		b, _ := json.Marshal(body)
		rd = bytes.NewReader(b)
	}
	req, _ := http.NewRequest(method, u, rd)
	req.Header.Set("Authorization", "Bearer "+token)
	req.Header.Set("Accept", "application/vnd.github+json")
	req.Header.Set("X-GitHub-Api-Version", "2022-11-28")
	req.Header.Set("User-Agent", "L4D2-PC-Port-Launcher/"+LauncherVersion)
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	resp, err := httpClient.Do(req)
	if err != nil {
		return 0, err
	}
	defer resp.Body.Close()
	data, _ := io.ReadAll(resp.Body)
	if resp.StatusCode >= 300 {
		var e struct {
			Message string `json:"message"`
		}
		json.Unmarshal(data, &e)
		return resp.StatusCode, fmt.Errorf("GitHub %s %s: %s %s", method, u, resp.Status, e.Message)
	}
	if out != nil {
		json.Unmarshal(data, out)
	}
	return resp.StatusCode, nil
}

type progressReader struct {
	r     io.Reader
	total int64
	done  int64
	last  time.Time
	say   func(string, ...any)
	name  string
}

func (p *progressReader) Read(b []byte) (int, error) {
	n, err := p.r.Read(b)
	p.done += int64(n)
	if time.Since(p.last) > time.Second || err == io.EOF {
		p.last = time.Now()
		p.say("  uploading %s  %.1f / %.1f MB", p.name, float64(p.done)/(1<<20), float64(p.total)/(1<<20))
	}
	return n, err
}

// Publish creates (or reuses) release v<Version> and uploads the files,
// replacing assets with the same name. Returns the release page URL.
func Publish(opt PublishOptions, say func(string, ...any)) (string, error) {
	token, source, err := githubToken()
	if err != nil {
		return "", err
	}
	say("GitHub login: %s", source)
	api := githubAPI + "/repos/" + opt.Repo
	tag := "v" + strings.TrimPrefix(opt.Version, "v")

	var rel struct {
		ID         int64  `json:"id"`
		HTMLURL    string `json:"html_url"`
		Prerelease bool   `json:"prerelease"`
		Assets     []struct {
			ID   int64  `json:"id"`
			Name string `json:"name"`
		} `json:"assets"`
	}
	status, err := ghAPI(token, "GET", api+"/releases/tags/"+url.PathEscape(tag), nil, &rel)
	if status == 404 {
		kind := "release"
		if opt.Prerelease {
			kind = "pre-release"
		}
		say("Creating %s %s on github.com/%s", kind, tag, opt.Repo)
		name := "L4D2 PC Port " + tag
		if opt.Prerelease {
			name += " (pre-release)"
		}
		_, err = ghAPI(token, "POST", api+"/releases", map[string]any{
			"tag_name": tag, "name": name, "body": opt.Notes, "prerelease": opt.Prerelease,
			"make_latest": map[bool]string{true: "false", false: "true"}[opt.Prerelease],
		}, &rel)
		if err != nil {
			return "", err
		}
	} else if err != nil {
		if status == 401 || status == 403 {
			return "", fmt.Errorf("%v\nThe GitHub login (%s) cannot write to %s - it needs \"Contents: write\" permission", err, source, opt.Repo)
		}
		return "", err
	} else {
		say("Updating existing release %s", tag)
		if rel.Prerelease != opt.Prerelease || opt.Notes != "" {
			patch := map[string]any{"prerelease": opt.Prerelease}
			if opt.Notes != "" {
				patch["body"] = opt.Notes
			}
			ghAPI(token, "PATCH", fmt.Sprintf("%s/releases/%d", api, rel.ID), patch, nil)
		}
	}

	for _, file := range opt.Files {
		name := filepath.Base(file)
		for _, a := range rel.Assets {
			if strings.EqualFold(a.Name, name) {
				say("  replacing existing %s", name)
				if _, err := ghAPI(token, "DELETE", fmt.Sprintf("%s/releases/assets/%d", api, a.ID), nil, nil); err != nil {
					return "", err
				}
			}
		}
		f, err := os.Open(file)
		if err != nil {
			return "", err
		}
		st, _ := f.Stat()
		up := fmt.Sprintf("%s/repos/%s/releases/%d/assets?name=%s", githubUploads, opt.Repo, rel.ID, url.QueryEscape(name))
		req, _ := http.NewRequest("POST", up, &progressReader{r: f, total: st.Size(), say: say, name: name})
		req.ContentLength = st.Size()
		req.Header.Set("Authorization", "Bearer "+token)
		req.Header.Set("Accept", "application/vnd.github+json")
		req.Header.Set("User-Agent", "L4D2-PC-Port-Launcher/"+LauncherVersion)
		ct := "application/octet-stream"
		if strings.HasSuffix(strings.ToLower(name), ".zip") {
			ct = "application/zip"
		}
		req.Header.Set("Content-Type", ct)
		resp, err := (&http.Client{}).Do(req)
		f.Close()
		if err != nil {
			return "", fmt.Errorf("upload of %s failed: %v", name, err)
		}
		body, _ := io.ReadAll(resp.Body)
		resp.Body.Close()
		if resp.StatusCode >= 300 {
			return "", fmt.Errorf("upload of %s failed: %s %s", name, resp.Status, string(body))
		}
		say("  uploaded %s (%.1f MB)", name, float64(st.Size())/(1<<20))
	}
	return rel.HTMLURL, nil
}
