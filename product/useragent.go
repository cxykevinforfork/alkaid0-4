package product

import (
	"runtime"
	"strings"
)

// UserAgentTemplate UA 字符串模板
const UserAgentTemplate = "Alkaid0/{version} ({system} {sysArch}) Go/{goVersion}"

// UserAgent UA 字符串（使用 NewReplacer 一次性替换，比链式 ReplaceAll 更高效）
var UserAgent = strings.NewReplacer(
	"{version}", Version,
	"{system}", runtime.GOOS,
	"{sysArch}", runtime.GOARCH,
	"{goVersion}", runtime.Version(),
).Replace(UserAgentTemplate)
