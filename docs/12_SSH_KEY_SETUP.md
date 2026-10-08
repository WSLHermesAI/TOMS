# 12 — SSH keys, and using them on another computer

> 中文版：[zh_TW/12_SSH_KEY_SETUP.md](zh_TW/12_SSH_KEY_SETUP.md)

> This page answers two things: **(1) how GitHub permissions work now**, and **(2) how to get push
> access on another computer**. The measured values come from `/home/fatming/Desktop/TOMS`
> (`main`, `cf15f19`). The overview of the whole process is in
> [`11_NEW_MACHINE_SETUP.md`](11_NEW_MACHINE_SETUP.md).

---

## 1. The short answer (for people in a hurry)

```bash
# To push from a new computer, the recommended way: generate a new key on that machine
ssh-keygen -t ed25519 -C "your@email"          # press Enter three times (or type a passphrase)
cat ~/.ssh/id_ed25519.pub                       # copy this line into GitHub → Settings → SSH and GPG keys → New SSH key
ssh -T git@github.com                           # "Hi <account>!" means success (note: the exit code can be 1 even then)
git remote set-url origin git@github.com:WSLHermesAI/TOMS.git
```

**If you only want to clone:** this repo is **public**, so `git clone` needs **no key at all**;
use the anonymous HTTPS URL (see §2). **Only pushing needs an SSH key.**

---

## 2. How permissions work

GitHub no longer accepts "account + password" for pushing. There are two ways now:

| Method | Where the credential lives | Used on this machine |
|---|---|---|
| **SSH key** | the private key file `~/.ssh/<key>`; the public key on the GitHub account | ✅ **in use** |
| **HTTPS + PAT** (Personal Access Token) | stored by a credential helper (Windows: Credential Manager) | ❌ not used (no helper, no `~/.git-credentials`) |

In one sentence: **the public key goes to GitHub, the private key stays only on your machine**.
When pushing, `git` calls `ssh`, which signs GitHub's challenge with the private key; GitHub checks
it with the public key on your account. So **the private key file itself is the login
credential**: leaking it means the account can be used by someone else.

### This machine today (measured)

| Item | Value |
|---|---|
| remote | `git@github.com:WSLHermesAI/TOMS.git` (SSH) |
| private key | `~/.ssh/id_ed25519`, ED25519, mode `600` |
| public key | `~/.ssh/id_ed25519.pub`, mode `644` |
| fingerprint | `SHA256:0AhSQKxzlEc4C+1+uP8z8wmeWOmh3SKnVAjubwNbFvg` |
| account | **WSLHermesAI** (`ssh -T git@github.com` answers `Hi WSLHermesAI!`) |
| `~/.ssh/config` | none (the default identity is used) |
| credential helper | not set; `~/.git-credentials` does not exist → **no password or PAT is stored** |
| repo visibility | **public** (an anonymous HTTPS read works → cloning needs no authentication) |
| `gh` CLI | not installed |

To see your own fingerprint (compare it with the one GitHub shows):

```bash
ssh-keygen -lf ~/.ssh/id_ed25519.pub
```

---

## 3. Getting push access on another computer: three ways

| | Way | Effort | Security | Advice |
|---|---|---|---|---|
| **A** | **Generate a new key** on that machine and add the public key to GitHub | medium | high (one key per machine, revocable on its own) | ✅ **recommended** |
| **B** | **Copy** this machine's `id_ed25519` over | low | low (the same identity in two places; more exposure) | ⏱ temporary / trusted private machines |
| **C** | Switch to **HTTPS + PAT** | medium | medium (a token can have an expiry and a scope) | 🔁 when you would rather not use SSH |

### A. Generate a new key on that computer (recommended)

```bash
ssh-keygen -t ed25519 -C "your@email"        # makes ~/.ssh/id_ed25519(.pub)
# for old-style RSA: ssh-keygen -t rsa -b 4096 -C "your@email"
```

1. Paste the **whole public key** (`~/.ssh/id_ed25519.pub`, one line) into
   GitHub → **Settings → SSH and GPG keys → New SSH key**.
   It only works on the right account: **WSLHermesAI** (with two accounts it is easy to paste it
   into the wrong one; see §6).
2. Check it (see §5): `ssh -T git@github.com` → you should see `Hi WSLHermesAI!`.
3. Set the remote (if the repo was cloned over HTTPS):

```bash
git remote set-url origin git@github.com:WSLHermesAI/TOMS.git
```

> If you only want to clone and never push, **none of this is needed**: HTTPS clones of a public
> repo need no authentication.

### B. Copy the existing key

```bash
# Copy from this machine (run on the NEW computer; use USB / a password manager / scp, never a public chat)
mkdir -p ~/.ssh && chmod 700 ~/.ssh
cp id_ed25519 id_ed25519.pub ~/.ssh/
chmod 600 ~/.ssh/id_ed25519
chmod 644 ~/.ssh/id_ed25519.pub
```

- **Fix the permissions:** SSH refuses a private key whose permissions are too open (Windows: see §4).
- This puts the same key on two machines: a break-in on either one exposes both. **Remove it from
  both when it is no longer needed**, or switch to way A then and delete the old key from GitHub.

### C. HTTPS + PAT

```bash
git remote set-url origin https://github.com/WSLHermesAI/TOMS.git
git config --global credential.helper manager     # Windows (built into Git for Windows)
# Linux: git config --global credential.helper store   ← stores it as plain text; less safe
git push            # the first time asks for the account and the token; type the PAT as the password
```

Make the PAT in GitHub → Settings → Developer settings → Personal access tokens. **Tick only the
permissions you need** (for repo contents: `repo`) and set an expiry date.

---

## 4. Windows notes

### 4.1 Windows and WSL have **two separate** `~/.ssh` (the easiest trap in this project)

| Environment | Key location | Used by |
|---|---|---|
| WSL2 (Linux) | `/home/<user>/.ssh/` | `git` in a WSL terminal |
| Windows | `C:\Users\<user>\.ssh\` | `git` in PowerShell / CMD / Git Bash / VS |

**They do not share keys:** the `id_ed25519` this machine (WSL) uses is **not** seen by Windows'
git, and the other way round. So `Permission denied (publickey)` on the first push from Windows
usually just means "Windows has no key yet", not that the key is broken.

Fix (pick one):
- do §3.A once on the Windows side too;
- or, after adding WSL's public key to GitHub, use the same private key on Windows (§3.B, copied to
  `C:\Users\<user>\.ssh\`);
- or push from WSL (which is how this machine works today).

### 4.2 Permissions (Windows' `chmod`)

Windows' OpenSSH checks the private key's ACL. When you see `UNPROTECTED PRIVATE KEY FILE`:

```powershell
icacls "$env:USERPROFILE\.ssh\id_ed25519" /inheritance:r /grant:r "$env:USERNAME:R"
```

### 4.3 ssh-agent (only needed with a passphrase)

```powershell
Get-Service ssh-agent                       # OpenSSH Client is built into Windows 10/11
Start-Service ssh-agent                     # setting it to start automatically needs admin rights
ssh-add "$env:USERPROFILE\.ssh\id_ed25519"
```

To install the OpenSSH Client (if missing):

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Client~~~~0.0.1.0     # needs admin
```

### 4.4 Which ssh Git for Windows uses

```bash
ssh -V                          # check the OpenSSH version
git config --get core.sshCommand    # if the project sets one
# to set it explicitly: git config --global core.sshCommand "C:/Windows/System32/OpenSSH/ssh.exe"
```

---

## 5. Checking and troubleshooting

> **Quickest: double-click `tools\check_git_ssh.cmd`** (`setup_new_pc.bat` also runs it after adding
> a key). It checks in the order git actually uses and offers fixes: whether `GIT_SSH_COMMAND` /
> `GIT_SSH` is broken, `core.sshCommand` (a path with spaces such as `C:/Program Files/...` breaks;
> use `C:/Windows/System32/OpenSSH/ssh.exe`), **whether TortoiseGit uses plink** (plink does not
> read `~/.ssh` keys → `No supported authentication methods available (server sent: publickey)`, and
> GitHub shows the key as *Never used*), the key fingerprint, the account logged in, whether the
> remote is SSH, `git fetch` and `git push --dry-run`, and `user.email`. Fixes are applied only after
> you press **Yes**; `-NoGui` only checks and changes nothing.

### 5.1 The right check (and a trap that misleads)

```bash
ssh -T git@github.com
# success: "Hi WSLHermesAI! You've successfully authenticated, but GitHub does not provide shell access."
```

⚠️ **GitHub provides no shell, so `ssh` exits with code 1 even on success.** Read the **output
text**, not the exit code, or a success looks like a failure.

To see which key was actually used:

```bash
ssh -vT git@github.com 2>&1 | grep -iE "Offering public key|Server accepts key|Authenticated to"
```

### 5.2 Common errors

| Message | Meaning | Fix |
|---|---|---|
| `Permission denied (publickey)` | GitHub does not know this key | the public key is not on the account / is on another account (§6) / this machine has no key at all (§4.1) |
| `Host key verification failed` | `known_hosts` is missing it or differs | `ssh-keyscan github.com >> ~/.ssh/known_hosts` (or confirm once interactively) |
| `UNPROTECTED PRIVATE KEY FILE` | the private key's permissions are too open | Linux `chmod 600`; Windows see §4.2 |
| `Could not open a connection to your authentication agent` | the agent is not running | §4.3, or use a key without a passphrase (your security trade-off) |
| asks for the passphrase every time | it must be typed each time | `ssh-add` it to the agent |
| push works but clone fails | they need different authentication | clone a public repo over HTTPS; only pushing needs the key |

---

## 6. Several accounts / several keys (`~/.ssh/config`)

This machine has no `~/.ssh/config`. If one machine must push to **different GitHub accounts**
(for example `WSLHermesAI` and `fatmingwang`, where the submodule lives), tell them apart with
Host aliases:

```
# ~/.ssh/config   (mode 600)
Host github-wslhermes
    HostName github.com
    User git
    IdentityFile ~/.ssh/id_ed25519

Host github-fatmingwang
    HostName github.com
    User git
    IdentityFile ~/.ssh/id_ed25519_fatmingwang
```

Then use the alias in the remote:

```bash
git remote set-url origin git@github-wslhermes:WSLHermesAI/TOMS.git
```

---

## 7. Security rules

1. **The private key (the file without `.pub`) never leaves the machine:** never commit it, paste it
   to an AI or a chat, or put it on a shared drive. Only the `.pub` is meant to be public.
2. **A passphrase is recommended**, remembered by ssh-agent. Without one, that file is your login
   (this machine is in that state today: `ssh` authenticates without any prompt, so keep it safe).
3. **One key per machine** (way A): if one leaks, revoke only that one.
4. **Revoke keys you no longer use:** GitHub → Settings → SSH keys → Delete; do it when changing
   machines, leaving, or losing a machine.
5. You can always compare fingerprints: `ssh-keygen -lf ~/.ssh/id_ed25519.pub` against the
   `SHA256:…` GitHub shows.
6. For CI / automation use a **Deploy key** (one repo, can be read-only) or GitHub Secrets, never a
   copy of a personal private key.

---

## 8. Quick reference

```bash
# ---- new machine: make a key and give it to GitHub ----
ssh-keygen -t ed25519 -C "your@email"
cat ~/.ssh/id_ed25519.pub                     # paste into GitHub → Settings → SSH and GPG keys

# ---- check (read the text, not the exit code) ----
ssh -T git@github.com                         # expect: Hi <account>!

# ---- clone (public repo, no authentication) ----
git clone --recurse-submodules https://github.com/WSLHermesAI/TOMS.git

# ---- to push later, switch the remote to SSH ----
git remote set-url origin git@github.com:WSLHermesAI/TOMS.git
git push
```
