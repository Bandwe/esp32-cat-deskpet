# 桌宠留言服务

这是供单个主人、单台桌宠使用的自托管留言服务。主人登录网页后预览并发送文字，服务端生成 240 × 280 的 4 位灰度图卡；设备通过 HTTPS 轮询当前版本、下载图卡并确认收到。每张图卡为 33,600 字节，留言最多 80 个字符。数据库只保留当前留言，不是公开访客留言板或聊天平台。

公开副本保留应用、前端、中文字体、初始化工具与本地测试，不包含运行配置、真实留言、设备凭据、历史部署包或生产环境运维脚本。下文的 `esp.example.com` 是保留示例域名，部署时必须替换成自己控制的域名。

## 本地运行

以下命令从本目录执行，使用 Python 3.12。先建立独立环境并安装固定版本的依赖：

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe tools/init_env.py --output .env --origin http://127.0.0.1:8088 --db-path data/esp.sqlite3 --local-http
.\.venv\Scripts\python.exe tools/run_local.py
```

用浏览器打开 `http://127.0.0.1:8088`。初始化工具会交互要求输入并再次确认至少 16 字符的管理密码，输入不回显；它只保存密码哈希和随机会话密钥。也可以加 `--generate-admin-password` 自动生成高熵密码，此时密码会在终端显示一次，应立即保存到密码管理器。生成器拒绝覆盖已有文件。

`.env.example` 仅说明格式，不要复制它当成可用配置；空的秘密字段会使应用拒绝启动。`tools/run_local.py` 读取本目录的 `.env`，只接受工具生成的单引号格式。默认开发服务仅监听本机，`--local-http` 也仅允许本机地址；真实桌宠连接使用下一节的 HTTPS 部署。

Linux/macOS 可以把上述 Python 命令换成 `.venv/bin/python`。本地 Flask 服务用于开发，不用于直接承接公网请求。

## 自托管部署

`Dockerfile` 和 `compose.yaml` 提供通用的 Linux 容器示例，未绑定任何现有服务器，也未自动申请证书。需要自己准备域名、Docker Compose、一个可信的反向代理及有效 TLS 证书。

先在部署机本目录创建生产 `.env`，不要复用本地 HTTP 配置：

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/python tools/init_env.py --output .env --origin https://esp.example.com --db-path /data/esp.sqlite3
docker compose up -d --build
```

Compose 将服务映射到部署机的 `127.0.0.1:8088`，使用非 root 用户运行 Gunicorn，并将 SQLite 存储在命名卷中。配置中的数据库位置固定为容器内 `/data/esp.sqlite3`。`docker compose down` 保留数据卷；加 `--volumes` 会删除卷和其中的数据，应避免误用。不要把 `.env` 放入镜像或版本库。

在同一主机的 Nginx 中配置一个 HTTPS 站点，下面片段只展示必要的转发关系；证书签发、续期和 HTTP 跳转由部署者配置：

```nginx
server {
    listen 443 ssl;
    server_name esp.example.com;
    ssl_certificate /etc/letsencrypt/live/esp.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/esp.example.com/privkey.pem;
    client_max_body_size 4k;
    gzip off;

    location / {
        proxy_pass http://127.0.0.1:8088;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-Host $host;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_set_header X-Forwarded-For $remote_addr;
    }
}
```

应用的 `ProxyFix` 信任恰好一层反向代理，因此后端端口必须保持不可被不可信客户端直接访问；代理应覆盖转发头。这个示例不适用于未经调整的多层 CDN/代理拓扑。`ESP_PUBLIC_ORIGIN` 必须与浏览器访问的 HTTPS origin 完全一致；生产环境保留安全 Cookie。

检查本机 `/healthz` 可确认应用与数据库基本可用，但不能证明设备完成配对或实际收到图卡。部署后应在浏览器完成登录，再通过真实设备确认接收状态。

## 设备配对与 HTTPS

登录后生成 8 位配对码，并在设备配置界面输入。配对码有效期 10 分钟，每个码最多允许 5 次错误尝试；重新生成会使前一个码失效。配对成功后码即失效，服务端签发新的设备令牌，旧设备令牌立即失效。网页也保留手工重置设备密钥的方式，返回值仅显示一次。

设备的服务地址应与此站点一致，固件中信任的根证书必须能验证站点提供的完整证书链，设备时间也必须正确。变更域名、证书签发机构或链路时，需要同时检查固件端的地址与根证书配置；不能通过跳过证书验证来解决连接问题。服务器配置 TLS 不会自动更新设备内的信任根。

设备 API 为 `/api/device/pair`、`/api/device/latest`、`/api/device/card/<revision>` 和 `/api/device/ack`。配对接口使用一次性配对码；其余接口使用 `Authorization: Bearer ...`。管理员接口需要登录会话，写操作还检查 CSRF 和来源。设备令牌应作为秘密保存，不能写进示例配置或日志。

## 配置与数据边界

| 变量 | 用途 |
| --- | --- |
| `ESP_SESSION_SECRET` | 初始化工具生成的随机会话/配对签名秘密；至少 32 字符 |
| `ESP_OWNER_PASSWORD_HASH` | 管理密码的 scrypt 哈希；不是明文密码 |
| `ESP_PUBLIC_ORIGIN` | 自己站点的完整 origin，无路径 |
| `ESP_DB_PATH` | SQLite 路径；容器示例固定为 `/data/esp.sqlite3` |
| `ESP_COOKIE_SECURE` | 生产环境为 `1`；本机 HTTP 开发可为 `0` |
| `ESP_FONT_PATH` | 可选字体路径；未设置时使用随附字体 |

应用限制登录尝试、验证输入和当前图卡版本，并在数据库中只存设备令牌的 SHA-256 摘要及配对码的 HMAC。当前留言文字、图卡和接收状态仍是数据库中的明文数据；这不是端到端加密服务。拥有服务器、数据库备份或设备存储读取权限的人可能取得相关内容，因此运行数据与备份都应私密保存。

这是一份个人自托管原型，单元测试不能代替独立安全审计、证书维护、系统更新和实际设备测试。它没有多用户隔离、双因素登录或高可用集群设计。公开副本未附带生产运维和密码轮换脚本；维护者需要管理自己的配置、备份与恢复流程。轮换会话秘密会使现有会话和配对码失效，但不会自动撤销已经签发的设备令牌；设备撤销应使用网页的重新配对或密钥重置功能。

## 测试

```powershell
.\.venv\Scripts\python.exe -B -m unittest discover -s tests -v
```

测试使用临时数据库和应用内测试客户端，覆盖渲染、鉴权、CSRF、限流、配置生成、配对码生命周期、令牌轮换竞态、版本号边界及旧数据库迁移，不需要生产配置或网络服务。测试数据中的凭据仅是隔离测试夹具。

依赖见 `requirements.txt`；中文字体的授权见 `fonts/OFL.txt`。本目录未新增项目整体许可证，字体许可证不自动适用于应用源码。
