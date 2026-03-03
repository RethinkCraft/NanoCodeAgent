# NanoCodeAgent

**NanoCodeAgent** 是一个教学型 AI Code Agent 工程，使用 C++ 实现，展示了如何构建一个能够理解自然语言指令、调用工具、操作文件系统的 AI 编码助手的核心机制。

## 特性

- 🤖 **LLM 集成**：调用 OpenAI 兼容 API（支持 GitHub Models、OpenAI、Azure 等）
- 🔧 **工具调用**：实现 Function Calling / Tool Use 协议（bash、read\_file、write\_file）
- 📁 **安全文件操作**：路径校验，防止路径穿越攻击
- 💻 **Shell 沙盒**：在隔离工作区内执行 Bash 命令
- 📖 **教学文档**：配套在线书籍，由 AI 自动维护

## 在线文档

> 📚 在线阅读：将来在 <https://Chris-godz.github.io/NanoCodeAgent/>

## 本地构建文档（mdBook）

```bash
# 安装 mdBook（需要 Rust）
cargo install mdbook

# 本地预览（访问 http://localhost:3000）
mdbook serve book -p 3000
```

## 构建项目

```bash
# 克隆（含子模块）
git clone --recurse-submodules https://github.com/Chris-godz/NanoCodeAgent.git
cd NanoCodeAgent

# 构建
./build.sh
```

## 依赖

- C++17 兼容编译器（GCC 11+ 或 Clang 14+）
- CMake 3.20+
- libcurl

## AI 文档维护

本项目使用 GitHub Actions 自动维护文档：

- **`pages.yml`**：每次推送 main 时自动构建并部署 GitHub Pages
- **`docgen.yml`**：检测到代码变更时，调用 GitHub Models 生成文档更新，并自动开 PR

详见 [`tools/docgen/`](tools/docgen/) 目录。

## 许可证

详见 [LICENSE](LICENSE) 文件（如有）。
