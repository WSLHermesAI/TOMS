# 12 — SSH 金鑰說明與「在另一台電腦上使用」

> 這份文件回答兩件事：**(1) GitHub 的權限現在是怎麼運作的**、**(2) 換一台電腦要怎麼取得 push 權限**。
> 實測數字取自 `/home/fatming/Desktop/TOMS`（`main`，`cf15f19`），指令與檔名保持英文，敘述為繁體中文。
> 流程面的總覽在 [`11_NEW_MACHINE_SETUP.md`](11_NEW_MACHINE_SETUP.md)。

---

## 1. 先講結論（給趕時間的人）

```bash
# 新電腦要 push，最建議的做法：在該機器產生一把新金鑰
ssh-keygen -t ed25519 -C "your@email"          # 直接 Enter 三次即可（若要 passphrase 就自行輸入）
cat ~/.ssh/id_ed25519.pub                       # 複製這一行，貼到 GitHub → Settings → SSH and GPG keys → New SSH key
ssh -T git@github.com                           # 看到 "Hi <帳號>!" 就成功（注意：就算成功，退出碼仍可能是 1）
git remote set-url origin git@github.com:WSLHermesAI/TOMS.git
```

**但如果你只是要 clone**：這個 repo 是**公開的**，所以 `git clone` **完全不需要任何金鑰**，
用 HTTPS 匿名網址即可（見 §2）。**只有 push 需要 SSH 金鑰。**

---

## 2. 權限是怎麼運作的

GitHub 不再接受「帳號＋密碼」推送。現在只有兩條路：

| 方式 | 憑證放在哪 | 本機有沒有用 |
|---|---|---|
| **SSH 金鑰** | 私鑰檔案 `~/.ssh/<key>`；公鑰存在 GitHub 帳號上 | ✅ **在用** |
| **HTTPS + PAT**（Personal Access Token） | 由 credential helper 存放（Windows：Credential Manager） | ❌ 未使用（沒有 helper、沒有 `~/.git-credentials`） |

原理一句話：**公鑰給 GitHub，私鑰只留在你的機器上**。推送時 `git` 會呼叫 `ssh`，
`ssh` 用私鑰對 GitHub 發出挑戰簽章；GitHub 用你帳號上的那把公鑰驗證。
所以**私鑰檔本身就是登入憑證**——外洩等於被盜用。

### 本機現況（實測）

| 項目 | 值 |
|---|---|
| remote | `git@github.com:WSLHermesAI/TOMS.git`（SSH） |
| 私鑰 | `~/.ssh/id_ed25519`，ED25519，權限 `600` |
| 公鑰 | `~/.ssh/id_ed25519.pub`，權限 `644` |
| 指紋 | `SHA256:0AhSQKxzlEc4C+1+uP8z8wmeWOmh3SKnVAjubwNbFvg` |
| 對應帳號 | **WSLHermesAI**（`ssh -T git@github.com` 回 `Hi WSLHermesAI!`） |
| `~/.ssh/config` | 無（使用預設 identity） |
| credential helper | 未設定；`~/.git-credentials` 不存在 → **沒有儲存任何密碼或 PAT** |
| repo 可見性 | **公開**（匿名 HTTPS 讀取測試通過 → clone 免認證） |
| `gh` CLI | 未安裝 |

查自己的指紋（可與 GitHub 網頁上顯示的比對）：

```bash
ssh-keygen -lf ~/.ssh/id_ed25519.pub
```

---

## 3. 在另一台電腦上取得 push 權限：三種做法

| | 做法 | 難度 | 安全性 | 建議 |
|---|---|---|---|---|
| **A** | 在該機器**產生新金鑰**，公鑰加到 GitHub | 中 | 高（一台一鑰，可單獨撤銷） | ✅ **建議** |
| **B** | **複製**本機的 `id_ed25519` 過去 | 低 | 低（同一身分存在兩處，外洩面變大） | ⏱ 臨時／可信任的私有機器 |
| **C** | 改用 **HTTPS + PAT** | 中 | 中（token 可設期限與範圍） | 🔁 不想碰 SSH 時 |

### A. 在該電腦產生新金鑰（建議）

```bash
ssh-keygen -t ed25519 -C "your@email"        # 產生 ~/.ssh/id_ed25519(.pub)
# 想用舊式 RSA：ssh-keygen -t rsa -b 4096 -C "your@email"
```

1. 把 **公鑰全文**（`~/.ssh/id_ed25519.pub`，一整行）貼到
   GitHub → **Settings → SSH and GPG keys → New SSH key**。
   帳號對了才會生效：**WSLHermesAI**（有兩個帳號時最容易貼錯，見 §6）。
2. 驗證（見 §5）：`ssh -T git@github.com` → 要看到 `Hi WSLHermesAI!`。
3. 設定 remote（若 repo 是用 HTTPS clone 的）：

```bash
git remote set-url origin git@github.com:WSLHermesAI/TOMS.git
```

> 只想 clone、不打算 push 的話，**這一步都不用做**：公開 repo 的 HTTPS clone 免認證。

### B. 複製既有金鑰

```bash
# 從本機複製（在「新電腦」上執行；用 USB / 密碼管理器 / scp 皆可，不要走公開聊天室）
mkdir -p ~/.ssh && chmod 700 ~/.ssh
cp id_ed25519 id_ed25519.pub ~/.ssh/
chmod 600 ~/.ssh/id_ed25519
chmod 644 ~/.ssh/id_ed25519.pub
```

- **權限一定要修**：SSH 拒絕載入權限過寬的私鑰（Windows 見 §4）。
- 這等於把同一把鑰匙放在兩台機器：任何一台被入侵都等於兩台；**不再需要時記得兩邊都清掉**，
  或是屆時改用做法 A 並把舊鑰從 GitHub 刪除。

### C. HTTPS + PAT

```bash
git remote set-url origin https://github.com/WSLHermesAI/TOMS.git
git config --global credential.helper manager     # Windows（Git for Windows 內建）
# Linux: git config --global credential.helper store   ← 會以明文存檔，較不安全
git push            # 第一次會要帳號與 token，把 PAT 當密碼輸入
```

PAT 到 GitHub → Settings → Developer settings → Personal access tokens 產生，
**只勾需要的權限**（對 repo 內容：`repo`）並設定有效期限。

---

## 4. Windows 專屬注意事項

### 4.1 Windows 與 WSL 是**兩套** `~/.ssh`（本專案最容易踩的坑）

| 環境 | 金鑰位置 | 誰在用 |
|---|---|---|
| WSL2 (Linux) | `/home/<user>/.ssh/` | WSL 終端機裡的 `git` |
| Windows | `C:\Users\<user>\.ssh\` | PowerShell / CMD / Git Bash / VS 裡的 `git` |

**兩者互不相通**：本機（WSL）用的 `id_ed25519` **不會**被 Windows 的 git 看到，
反之亦然。所以在 Windows 上第一次 push 出現 `Permission denied (publickey)`，
通常就只是「Windows 這邊還沒有金鑰」，不是金鑰壞了。

處理方式（擇一）：
- 在 Windows 那邊也做一次 §3.A；
- 或把 WSL 的公鑰貼上 GitHub 後，在 Windows 用同一把私鑰（§3.B 複製到 `C:\Users\<user>\.ssh\`）；
- 或直接從 WSL 做 push（本機現在就是這樣運作的）。

### 4.2 權限（Windows 版的 `chmod`）

Windows 的 OpenSSH 會檢查私鑰 ACL；出現 `UNPROTECTED PRIVATE KEY FILE` 時：

```powershell
icacls "$env:USERPROFILE\.ssh\id_ed25519" /inheritance:r /grant:r "$env:USERNAME:R"
```

### 4.3 ssh-agent（有 passphrase 時才需要）

```powershell
Get-Service ssh-agent                       # Windows 10/11 內建 OpenSSH Client
Start-Service ssh-agent                     # 需管理員權限設定為自動啟動
ssh-add "$env:USERPROFILE\.ssh\id_ed25519"
```

裝 OpenSSH Client（若缺）：

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Client~~~~0.0.1.0     # 需管理員
```

### 4.4 Git for Windows 用哪個 ssh

```bash
ssh -V                          # 確認 OpenSSH 版本
git config --get core.sshCommand    # 若專案特別指定
# 需要明確指定時：git config --global core.sshCommand "C:/Windows/System32/OpenSSH/ssh.exe"
```

---

## 5. 驗證與排錯

> **最快：雙擊 `tools\check_git_ssh.cmd`**（`setup_new_pc.bat` 在加完金鑰後也會自動跑）。
> 依 git 實際使用的順序檢查並提議修正：`GIT_SSH_COMMAND`／`GIT_SSH` 是否壞掉、`core.sshCommand`
> （路徑含空白如 `C:/Program Files/...` 會壞；改用 `C:/Windows/System32/OpenSSH/ssh.exe`）、
> **TortoiseGit 是否用 plink**（plink 不讀 `~/.ssh` 金鑰 → `No supported authentication methods available (server sent: publickey)`，
> GitHub 上金鑰顯示 *Never used*）、金鑰指紋、登入帳號、remote 是否為 SSH、`git fetch` 與 `git push --dry-run`、`user.email`。
> 修正只在按「是」後才套用；`-NoGui` 只檢查不修改。

### 5.1 正確的驗證方式（含一個會誤判的陷阱）

```bash
ssh -T git@github.com
# 成功：「Hi WSLHermesAI! You've successfully authenticated, but GitHub does not provide shell access.」
```

⚠️ **GitHub 不提供 shell，所以連成功時 `ssh` 的退出碼也是 1**。
請看**輸出文字**，不要看退出碼，否則會把成功誤判成失敗。

想知道「到底用了哪一把金鑰」：

```bash
ssh -vT git@github.com 2>&1 | grep -iE "Offering public key|Server accepts key|Authenticated to"
```

### 5.2 常見錯誤對照

| 訊息 | 意思 | 處理 |
|---|---|---|
| `Permission denied (publickey)` | GitHub 不認識這把金鑰 | 公鑰沒加到帳號／加到別的帳號（§6）／這台機器根本沒有金鑰（§4.1） |
| `Host key verification failed` | `known_hosts` 沒有或不同 | `ssh-keyscan github.com >> ~/.ssh/known_hosts`（或互動確認一次） |
| `UNPROTECTED PRIVATE KEY FILE` | 私鑰權限太寬 | Linux `chmod 600`；Windows 見 §4.2 |
| `Could not open a connection to your authentication agent` | 沒啟動 agent | §4.3，或改用無 passphrase 的金鑰（安全性自行取捨） |
| 一直問 passphrase | 每次都要輸入 | `ssh-add` 進 agent |
| push 成功但 clone 失敗 | 兩者認證需求不同 | clone 公開 repo 用 HTTPS 即可；push 才需要金鑰 |

---

## 6. 多帳號／多金鑰（`~/.ssh/config`）

本專案本機沒有 `~/.ssh/config`；但若同一台機器要對**不同 GitHub 帳號**推送
（例如 `WSLHermesAI` 與 submodule 所在的 `fatmingwang`），用 Host 別名區分：

```
# ~/.ssh/config   （權限 600）
Host github-wslhermes
    HostName github.com
    User git
    IdentityFile ~/.ssh/id_ed25519

Host github-fatmingwang
    HostName github.com
    User git
    IdentityFile ~/.ssh/id_ed25519_fatmingwang
```

之後 remote 用別名：

```bash
git remote set-url origin git@github-wslhermes:WSLHermesAI/TOMS.git
```

---

## 7. 安全守則

1. **私鑰（沒有 `.pub` 的那個）永遠不外流**：不提交進版控、不貼給 AI／聊天室、不放共用磁碟。
   只有 `.pub` 是設計來公開的。
2. **建議加 passphrase**，並用 ssh-agent 記住；不加 passphrase 的話，那個檔案就是你的登入憑證
   （本機目前就是這種狀態——`ssh` 在無互動模式下直接通過認證，請特別小心保管）。
3. **一台機器一把金鑰**（做法 A）：洩漏時只撤銷那一把，不必動全部。
4. **不再使用就撤銷**：GitHub → Settings → SSH keys → Delete；換機、離職、機器遺失都要做。
5. 公鑰指紋可隨時比對：`ssh-keygen -lf ~/.ssh/id_ed25519.pub` 對照 GitHub 上顯示的 `SHA256:…`。
6. CI／自動化請用 **Deploy key**（單一 repo、可設唯讀）或 GitHub Secrets，不要複製個人私鑰。

---

## 8. 快速指令卡

```bash
# ---- 新機器：產生金鑰並提供給 GitHub ----
ssh-keygen -t ed25519 -C "your@email"
cat ~/.ssh/id_ed25519.pub                     # 貼到 GitHub → Settings → SSH and GPG keys

# ---- 驗證（看文字，不看退出碼）----
ssh -T git@github.com                         # 期望：Hi <帳號>!

# ---- clone（公開 repo，免認證）----
git clone --recurse-submodules https://github.com/WSLHermesAI/TOMS.git

# ---- 之後要 push，把 remote 換成 SSH ----
git remote set-url origin git@github.com:WSLHermesAI/TOMS.git
git push
```
