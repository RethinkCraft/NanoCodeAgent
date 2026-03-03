# 第 8 章：配置与日志系统

## 配置系统（`config.cpp`）

NanoCodeAgent 使用 INI 格式的配置文件，通过 `--config` 参数指定。

### 配置文件结构

```ini
[llm]
endpoint = https://models.inference.ai.azure.com
model = openai/gpt-4o-mini
api_key = <your-api-key>
# 可选：系统提示词
system_prompt = 你是一个 C++ 编程助手

[agent]
# 工作区目录（相对或绝对路径）
workspace = ./my_workspace
# 单次对话最大轮数
max_turns = 20
# bash 命令超时（秒）
bash_timeout = 30
```

### 配置项说明

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `llm.endpoint` | — | LLM API 端点 URL（必填） |
| `llm.model` | — | 模型名称（必填） |
| `llm.api_key` | — | API 密钥（必填） |
| `llm.system_prompt` | 内置默认 | 系统提示词 |
| `agent.workspace` | `./workspace` | 工作区目录 |
| `agent.max_turns` | 20 | 最大对话轮数 |
| `agent.bash_timeout` | 30 | Bash 超时秒数 |

## 日志系统（`logger.cpp`）

### 日志级别

```
DEBUG < INFO < WARN < ERROR
```

默认级别为 `INFO`，可通过配置或环境变量调整：

```bash
NANO_LOG_LEVEL=DEBUG ./build/NanoCodeAgent --config config.ini
```

### 日志格式

```
[2025-01-01 12:00:00] [INFO] [llm] Sending request to model gpt-4o-mini
[2025-01-01 12:00:01] [DEBUG] [tool] Executing bash: ls -la
```

### 日志输出

默认输出到 **stderr**，不影响 Agent 的标准输出（供程序化使用）。

<!-- TODO: 补充日志文件轮转配置 -->
