import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { dirname, extname, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { spawn } from "node:child_process";

const host = "127.0.0.1";
const port = 8765;
const root = dirname(fileURLToPath(import.meta.url));
const startPage = "/serial_curve_plotter.html";
const url = `http://${host}:${port}${startPage}`;
const shouldOpenBrowser = !process.argv.includes("--no-open");
const contentTypes = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".csv": "text/csv; charset=utf-8",
};

const server = createServer(async (request, response) => {
  try {
    const pathname = decodeURIComponent(new URL(request.url, url).pathname);
    const requested = pathname === "/" ? startPage : pathname;
    const filePath = resolve(root, requested.replace(/^\/+/, ""));
    if (filePath !== root && !filePath.startsWith(root + sep)) {
      response.writeHead(403).end("Forbidden");
      return;
    }
    const body = await readFile(filePath);
    response.writeHead(200, {
      "Content-Type": contentTypes[extname(filePath).toLowerCase()] || "application/octet-stream",
      "Cache-Control": "no-store",
      "X-Content-Type-Options": "nosniff",
    });
    response.end(body);
  } catch (error) {
    response.writeHead(error?.code === "ENOENT" ? 404 : 500).end("Not found");
  }
});

server.on("error", error => {
  if (error.code === "EADDRINUSE") {
    console.error(`端口 ${port} 已被占用；请关闭旧的上位机服务后重试。`);
  } else {
    console.error(error);
  }
  process.exitCode = 1;
});

server.listen(port, host, () => {
  console.log(`气体传感上位机已启动：${url}`);
  console.log("保持此窗口开启；按 Ctrl+C 停止本地服务。");
  if (shouldOpenBrowser && process.platform === "win32") {
    const opener = spawn("cmd.exe", ["/c", "start", "", url], {
      detached: true,
      stdio: "ignore",
      windowsHide: true,
    });
    opener.unref();
  }
});
