"""Chinese display text for the SHA-512 pinned ESP Web Tools 10.5.2 bundle.

Only UI strings change. The ESP ROM protocol, flashing algorithm and binary
verification remain upstream code. Fail closed if required upstream text drifts.
"""
import re


def localize_dialog(source):
    replacements = {
        '"Logs"': '"串口日志"',
        '"Connecting"': '"正在连接"',
        '"Disconnected"': '"设备已断开，请检查 USB 连接"',
        '"Erase device"': '"清除设备数据"',
        '"Erase User Data"': '"清除用户数据"',
        '"Installing"': '"正在安装"',
        '"Erasing"': '"正在清除数据"',
        '"Wrapping up"': '"正在完成安装"',
        '"Installation failed"': '"安装失败"',
        '"Preparing installation"': '"正在准备安装"',
        '"Confirm Installation"': '"确认安装"',
        'label="Installation complete!"': 'label="安装完成！"',
        'Do you want to erase the device before installing': '安装',
        '? All data on the device will be lost.': '前，是否清除设备上的全部数据？重装时不勾选，可保留安装方向和 FPS 设置。',
        'All data on the device will be erased.': '设备上的全部数据都将被清除。',
        'Keep this page visible for fastest installation.': '请保持页面在前台，并保持 USB 连接。',
        'Do you want to ${': '是否${',
        '"update to"': '"更新至"',
        '"install"': '"安装"',
        'Your device is running': '设备当前运行',
        'Logs & Console': '串口日志',
        'Download Logs': '下载日志',
        'Reset Device': '重启设备',
        '`Install ${': '`安装 ${',
        '`Update ${': '`更新 ${',
        '"Failed to download manifest"': '"无法读取固件信息，请重新打开安装文件"',
        '"Serial port is not readable/writable. Close any other application using it and try again."': '"串口不可读写。请关闭其他占用串口的程序后重试。"',
        '"Serial port is not ready. Close any other application using it and try again."': '"串口尚未就绪。请关闭其他串口工具后重试。"',
        '"Failed to initialize. Try resetting your device or holding the BOOT button while clicking INSTALL."': '"无法进入烧录模式。请重启设备，或按住 BOOT 键再点击安装。"',
        '`Your ${': '`此安装包不支持 ${',
        '} board is not supported.`': '} 开发板。`',
        '`Downloading firmware ${': '`读取固件 ${',
        '} failed: ${': '} 失败：${',
    }
    required = {'"Erase device"', '"Confirm Installation"', 'label="Installation complete!"',
                'Do you want to erase the device before installing', '"Preparing installation"'}
    for old, new in replacements.items():
        if old in required and old not in source:
            raise ValueError('ESP Web Tools localization source drift: ' + old)
        source = source.replace(old, new)
    for old, new in {'Back':'返回', 'Next':'下一步', 'Install':'安装', 'Close':'关闭',
                     'Erase device':'清除设备数据', 'Erase User Data':'清除用户数据'}.items():
        source = re.sub(r'(>\s*)' + re.escape(old) + r'(\s*<)', lambda m:m[1]+new+m[2], source)
    source = re.sub(r'This will take\s*\$\{[^}]+\}\.', '安装约需 1–2 分钟。', source)
    source = re.sub(r'Do you want to reset your device and erase all user data from your\s*device\?', '是否重置设备并清除全部用户数据？', source)
    # Errors may include an upstream technical detail; retain it after a Chinese label.
    source = source.replace('return["Error",', 'return["发生错误",')
    source = source.replace('<ew-checkbox touch-target=', '<ew-checkbox aria-label="清除设备数据" touch-target=')
    source = source.replace('<ew-icon-button slot="headline"', '<ew-icon-button aria-label="关闭安装窗口" slot="headline"')
    return source


def theme_dialog(source):
    start = source.index('--roboto-font: Roboto, system-ui;')
    end = source.index('`;export', start)
    theme = source[start:end]
    for old, new in {
        '--roboto-font: Roboto, system-ui;': '--roboto-font: system-ui, "PingFang SC", "Microsoft YaHei", sans-serif;',
        '--text-color: rgba(0, 0, 0, 0.6);': '--text-color: #c1d0d8; --md-sys-color-on-surface: #eaf2f4; --md-sys-color-on-surface-variant: #a4b7c1; --md-sys-color-outline: #718893;',
        '--danger-color: #db4437;': '--danger-color: #ff8187;',
        '--md-sys-color-primary: #03a9f4;': '--md-sys-color-primary: #68f0c4;',
        '--md-sys-color-on-primary: #fff;': '--md-sys-color-on-primary: #08251d;',
        '--md-sys-color-surface: #fff;': '--md-sys-color-surface: #101b24;',
        '--md-sys-color-surface-container: #fff;': '--md-sys-color-surface-container: #101b24;',
        '--md-sys-color-surface-container-high: #fff;': '--md-sys-color-surface-container-high: #14232e;',
        '--md-sys-color-surface-container-highest: #f5f5f5;': '--md-sys-color-surface-container-highest: #1b2e3a;',
        '--md-sys-color-secondary-container: #e0e0e0;': '--md-sys-color-secondary-container: #203831;',
    }.items():
        if old not in theme:
            raise ValueError('ESP Web Tools theme source drift: ' + old)
        theme = theme.replace(old,new)
    return source[:start] + theme + source[end:]
