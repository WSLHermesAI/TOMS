# Windows（Visual Studio 2022）建置指南

本文件說明如何在 **Windows + Visual Studio 2022** 上建置 TOMS 的桌面版（Vulkan）。

## 1. 需求

請先安裝以下工具：

- **Visual Studio 2022**
- Visual Studio 工作負載：**Desktop development with C++**
- **CMake tools for Windows**
- **Windows 10 / 11 SDK**
- **Vulkan SDK**
- **GLM**（僅需要標頭檔）

## 2. 必要環境變數

此專案的 `CMakeLists.txt` 會讀取以下環境變數：

- `VULKAN_SDK`
- `GLM_DIR`

### 範例

```bat
setx VULKAN_SDK "C:\VulkanSDK\1.3.280.0"
setx GLM_DIR "C:\libs\glm"
```

> `GLM_DIR` 應該指向包含 `glm/` 目錄的路徑。

## 3. 用 Visual Studio 開啟專案

1. 開啟 **Visual Studio 2022**
2. 選擇 **File → Open → Folder...**
3. 開啟 TOMS 專案根目錄
4. Visual Studio 會自動讀取 `CMakePresets.json`
5. 選用 preset：**`vs2022-x64`**

這個專案已經設定好：

- `tower_vulkan` 是主要桌面執行目標
- `tower_vulkan` 會被設成 Visual Studio 的 startup project

## 4. 建置方式

### 方式 A：在 Visual Studio 內建置

1. 先選 `vs2022-x64` preset
2. 切換組態為 `Debug` 或 `Release`
3. 按 **Build** 或 **F5**

### 方式 B：使用 CMake 指令列

在 Visual Studio Developer Command Prompt 或 PowerShell 執行：

```bat
cmake --preset vs2022-x64
cmake --build --preset vs2022-release
```

如果要建置 Debug：

```bat
cmake --build --preset vs2022-debug
```

也可以直接指定 target：

```bat
cmake --build build --config Release --target tower_vulkan
```

## 5. 執行結果

建置完成後，主要執行檔會是：

- `tower_vulkan.exe`

專案會在 post-build 階段自動複製以下資源到執行檔旁邊：

- `assets/`
- `data/`
- shader `.spv` 檔案

所以通常可以直接從 Visual Studio 或 build 輸出資料夾執行。

## 6. Shader 注意事項

如果執行時遇到 shader 載入問題，請確認：

- `VULKAN_SDK` 已正確設定
- Vulkan SDK 內有 `glslangValidator.exe`
- `assets/shaders/` 底下有需要的 `.spv` 檔案

若 `.spv` 不存在，CMake 會嘗試從 `.vert` / `.frag` 重新編譯。

## 7. 常見問題

### 7.1 找不到 Vulkan

請確認：

- Vulkan SDK 已安裝
- `VULKAN_SDK` 指向正確位置
- Visual Studio 使用的是 x64 組態

### 7.2 找不到 GLM

請確認：

- `GLM_DIR` 指向正確的 GLM 標頭目錄
- 路徑內包含 `glm/vec3.hpp` 這類檔案

### 7.3 Visual Studio 開啟後沒有選到正確 target

如果 F5 不是啟動 `tower_vulkan`，請在 CMake targets 中手動選擇 `tower_vulkan`。

## 8. 建議流程

如果你是第一次在 Windows 上建置這個專案，建議順序如下：

1. 安裝 Visual Studio 2022 與 C++ 工作負載
2. 安裝 Vulkan SDK
3. 準備 GLM
4. 設定 `VULKAN_SDK` 與 `GLM_DIR`
5. 用 Visual Studio 開啟專案根目錄
6. 選 `vs2022-x64`
7. Build `tower_vulkan`
8. F5 執行

---

如果你想，我也可以再補一份「**Windows + vcpkg**」版本的建置文件。