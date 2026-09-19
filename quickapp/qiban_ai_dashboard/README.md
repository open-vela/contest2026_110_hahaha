# qiban_ai_dashboard

`骑伴 AI 智能电动车中控屏` 的快应用工程目录。

## 目录用途

- `package.json`：快应用构建脚本与依赖
- `src/app.ux`：应用入口
- `src/manifest.json`：快应用清单
- `src/pages/index/`：当前 MVP 首页
- `src/common/logo.png`：应用图标占位资源

## 本地构建

```bash
npm install
npm run build
```

`npm run build` 使用工具链内置 debug 证书，适合本地联调。

## Release 打包

`npm run release` 需要本地存在 release 签名文件：

```text
sign/release/private.pem
sign/release/certificate.pem
```

本仓不提交私钥。首次打 release 包前可生成一套本地自签名证书：

```bash
./tools/prepare_release_sign.sh
npm run release
```

生成的 `release.rpk` 用于比赛提交和真机部署。
