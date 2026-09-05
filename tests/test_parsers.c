#include "test_runner.h"
#include "core/format.h"

#include <glib/gstdio.h>

static gchar *
write_tmp(const gchar *name, const gchar *content)
{
    gchar *path = g_build_filename(g_get_tmp_dir(), name, NULL);
    g_file_set_contents(path, content, -1, NULL);
    return path;
}

void test_parsers(void)
{
    /* ---------- INI ---------- */
    {
        gchar *p = write_tmp("lr-test-1.ini",
                             "# 顶部注释\n"
                             "[server]\n"
                             "Port = 22  # 端口\n"
                             "Enable = yes\n"
                             "; 第二注释\n"
                             "Name = \"my host\"\n");
        LrConfigFile *f = lr_parse_config(p);

        TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_INI);
        TEST_ASSERT(f->parsed);
        TEST_ASSERT(f->items->len == 3);

        LrConfigItem *it = g_ptr_array_index(f->items, 0);
        TEST_ASSERT_STR_EQ(it->key, "Port");
        TEST_ASSERT(it->type == LR_VALUE_NUMBER);
        TEST_ASSERT_STR_EQ(it->data, "22");
        TEST_ASSERT_STR_EQ(it->section, "server");
        /* 上方注释 + 行内注释合并显示 */
        TEST_ASSERT_STR_EQ(it->comment, "顶部注释\n端口");

        it = g_ptr_array_index(f->items, 1);
        TEST_ASSERT_STR_EQ(it->key, "Enable");
        TEST_ASSERT(it->type == LR_VALUE_BOOL);
        TEST_ASSERT_STR_EQ(it->data, "yes");
        /* 注释未被消费：下方配置项共用上方最近一条注释 */
        TEST_ASSERT_STR_EQ(it->comment, "顶部注释");

        it = g_ptr_array_index(f->items, 2);
        TEST_ASSERT_STR_EQ(it->key, "Name");
        TEST_ASSERT(it->type == LR_VALUE_STRING);
        TEST_ASSERT_STR_EQ(it->data, "my host");
        TEST_ASSERT_STR_EQ(it->comment, "第二注释");

        lr_config_file_free(f);
        g_unlink(p);
        g_free(p);
    }

    /* ---------- TOML ---------- */
    {
        gchar *p = write_tmp("lr-test-toml.toml",
                             "title = \"linux-regedit demo\"\n"
                             "[server]\n"
                             "host = \"127.0.0.1\"\n"
                             "port = 8080\n"
                             "debug = false\n"
                             "[logging]\n"
                             "level = \"info\"   # 日志级别\n"
                             "# 轮转说明\n"
                             "rotate = true\n");
        LrConfigFile *f = lr_parse_config(p);

        TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_TOML);
        TEST_ASSERT(f->parsed);
        TEST_ASSERT(f->items->len == 6);

        LrConfigItem *it = g_ptr_array_index(f->items, 0);
        TEST_ASSERT_STR_EQ(it->key, "title");
        TEST_ASSERT(it->type == LR_VALUE_STRING);
        TEST_ASSERT_STR_EQ(it->data, "linux-regedit demo");
        TEST_ASSERT(it->comment == NULL);

        it = g_ptr_array_index(f->items, 1);
        TEST_ASSERT_STR_EQ(it->key, "host");
        TEST_ASSERT_STR_EQ(it->section, "server");
        TEST_ASSERT_STR_EQ(it->data, "127.0.0.1");

        it = g_ptr_array_index(f->items, 2);
        TEST_ASSERT_STR_EQ(it->key, "port");
        TEST_ASSERT(it->type == LR_VALUE_NUMBER);
        TEST_ASSERT_STR_EQ(it->data, "8080");

        it = g_ptr_array_index(f->items, 3);
        TEST_ASSERT_STR_EQ(it->key, "debug");
        TEST_ASSERT(it->type == LR_VALUE_BOOL);

        it = g_ptr_array_index(f->items, 4);
        TEST_ASSERT_STR_EQ(it->key, "level");
        TEST_ASSERT_STR_EQ(it->section, "logging");
        TEST_ASSERT_STR_EQ(it->comment, "日志级别");

        it = g_ptr_array_index(f->items, 5);
        TEST_ASSERT_STR_EQ(it->key, "rotate");
        TEST_ASSERT(it->type == LR_VALUE_BOOL);
        TEST_ASSERT_STR_EQ(it->comment, "轮转说明");

        lr_config_file_free(f);
        g_unlink(p);
        g_free(p);
    }

    /* ---------- 扁平 KeyValue ---------- */
    {
        gchar *p = write_tmp("lr-test-2.conf",
                             "# 环境变量\n"
                             "PATH=/usr/local/bin:/usr/bin\n"
                             "LANG=en_US.UTF-8\n"
                             "DEBUG=false\n");
        LrConfigFile *f = lr_parse_config(p);

        TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_KV);
        TEST_ASSERT(f->parsed);
        TEST_ASSERT(f->items->len == 3);

        LrConfigItem *it = g_ptr_array_index(f->items, 0);
        TEST_ASSERT_STR_EQ(it->key, "PATH");
        TEST_ASSERT(it->type == LR_VALUE_STRING);
        TEST_ASSERT_STR_EQ(it->comment, "环境变量");

        /* 同一条注释说明其下方多个配置项 */
        it = g_ptr_array_index(f->items, 1);
        TEST_ASSERT_STR_EQ(it->key, "LANG");
        TEST_ASSERT_STR_EQ(it->comment, "环境变量");

        it = g_ptr_array_index(f->items, 2);
        TEST_ASSERT_STR_EQ(it->key, "DEBUG");
        TEST_ASSERT(it->type == LR_VALUE_BOOL);
        TEST_ASSERT_STR_EQ(it->comment, "环境变量");

        lr_config_file_free(f);
        g_unlink(p);
        g_free(p);
    }

    /* ---------- systemd unit ---------- */
    {
        gchar *p = write_tmp("lr-test-3.service",
                             "[Unit]\n"
                             "Description=My demo service\n"
                             "After=network.target\n"
                             "\n"
                             "[Service]\n"
                             "Type=simple\n"
                             "Restart=on-failure\n"
                             "# 运行用户\n"
                             "User=www-data\n");
        LrConfigFile *f = lr_parse_config(p);

        TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_SYSTEMD);
        TEST_ASSERT(f->parsed);
        TEST_ASSERT(f->items->len == 5);

        LrConfigItem *it = g_ptr_array_index(f->items, 0);
        TEST_ASSERT_STR_EQ(it->key, "Description");
        TEST_ASSERT_STR_EQ(it->section, "Unit");
        TEST_ASSERT(it->type == LR_VALUE_STRING);

        it = g_ptr_array_index(f->items, 2);
        TEST_ASSERT_STR_EQ(it->key, "Type");
        TEST_ASSERT_STR_EQ(it->section, "Service");

        it = g_ptr_array_index(f->items, 3);
        TEST_ASSERT_STR_EQ(it->key, "Restart");
        TEST_ASSERT(it->type == LR_VALUE_STRING);
        TEST_ASSERT_STR_EQ(it->data, "on-failure");

        /* 最后一项 User 应带注释 */
        lr_config_file_free(f);

        /* 重新解析以检查注释归属 */
        f = lr_parse_config(p);
        LrConfigItem *last = g_ptr_array_index(f->items, f->items->len - 1);
        TEST_ASSERT_STR_EQ(last->key, "User");
        TEST_ASSERT_STR_EQ(last->comment, "运行用户");

        lr_config_file_free(f);
        g_unlink(p);
        g_free(p);
    }

    /* ---------- 未知格式（文本兜底） ---------- */
    {
        gchar *p = write_tmp("lr-test-4.log", "这是一个普通日志\n第二行\n");
        TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_UNKNOWN);
        TEST_ASSERT(!lr_format_supported(LR_FORMAT_UNKNOWN));
        g_unlink(p);
        g_free(p);
    }

    /* ---------- shebang 脚本：以文本形式打开 ---------- */
    {
        gchar *p = write_tmp("lr-test-6.sh",
                             "#!/bin/bash\n"
                             "# 配置脚本\n"
                             "echo hello\n");
        TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_UNKNOWN);
        TEST_ASSERT(!lr_format_supported(LR_FORMAT_UNKNOWN));
        g_unlink(p);
        g_free(p);
    }

    /* ---------- 关键字-参数（sshd_config 风格） ---------- */
    {
        gchar *p = write_tmp("lr-test-5.conf",
                             "# 注释行\n"
                             "#Port 22\n"
                             "Include /etc/ssh/sshd_config.d/*.conf\n"
                             "KbdInteractiveAuthentication no\n"
                             "Port 22\n"
                             "Subsystem\tsftp\t/usr/lib/openssh/sftp-server\n");
        LrConfigFile *f = lr_parse_config(p);

        TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_KEYWORD);
        TEST_ASSERT(f->parsed);
        TEST_ASSERT(f->items->len == 5);

        /* 被注释的配置：#Port 22 → Port 未启用 */
        LrConfigItem *it = g_ptr_array_index(f->items, 0);
        TEST_ASSERT_STR_EQ(it->key, "Port");
        TEST_ASSERT_STR_EQ(it->data, "22");
        TEST_ASSERT(it->type == LR_VALUE_NUMBER);
        TEST_ASSERT(!it->enabled);
        /* 上方最近一条说明文字作为备注 */
        TEST_ASSERT_STR_EQ(it->comment, "注释行");

        it = g_ptr_array_index(f->items, 1);
        TEST_ASSERT_STR_EQ(it->key, "Include");
        TEST_ASSERT_STR_EQ(it->data, "/etc/ssh/sshd_config.d/*.conf");
        TEST_ASSERT(it->type == LR_VALUE_STRING);
        TEST_ASSERT(it->enabled);
        TEST_ASSERT_STR_EQ(it->comment, "注释行");

        it = g_ptr_array_index(f->items, 2);
        TEST_ASSERT_STR_EQ(it->key, "KbdInteractiveAuthentication");
        TEST_ASSERT_STR_EQ(it->data, "no");
        TEST_ASSERT(it->type == LR_VALUE_BOOL);

        it = g_ptr_array_index(f->items, 3);
        TEST_ASSERT_STR_EQ(it->key, "Port");
        TEST_ASSERT_STR_EQ(it->data, "22");
        TEST_ASSERT(it->type == LR_VALUE_NUMBER);

        /* Tab 分隔 */
        it = g_ptr_array_index(f->items, 4);
        TEST_ASSERT_STR_EQ(it->key, "Subsystem");
        TEST_ASSERT_STR_EQ(it->data, "sftp /usr/lib/openssh/sftp-server");

        lr_config_file_free(f);
        g_unlink(p);
        g_free(p);
    }

    /* ---------- 纯英文文本（如 /etc/legal）：应判为未知格式 ---------- */
    {
        gchar *p = write_tmp("lr-test-7.txt",
                             "The programs included with the Ubuntu system "
                             "are free software;\n"
                             "the exact distribution terms for each program "
                             "are described in the\n"
                             "individual files in /usr/share/doc/*/copyright.\n");
        TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_UNKNOWN);
        TEST_ASSERT(!lr_format_supported(LR_FORMAT_UNKNOWN));
        g_unlink(p);
        g_free(p);
    }

    /* ---------- JSON：识别与解析 ---------- */
    {
        gchar *p = write_tmp("lr-test-8.json",
                             "{\n"
                             "  \"name\": \"linux-regedit\",\n"
                             "  \"version\": 1,\n"
                             "  \"enabled\": true,\n"
                             "  \"tags\": [\"a\", \"b\"],\n"
                             "  \"window\": {\"width\": 800, \"height\": 600}\n"
                             "}\n");
        LrConfigFile *f = lr_parse_config(p);

        TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_JSON);
        TEST_ASSERT(lr_format_supported(LR_FORMAT_JSON));
        TEST_ASSERT(f->parsed);

        lr_config_file_free(f);
        g_unlink(p);
        g_free(p);
    }

    /* ---------- 真实示例文件（testdata/，meson 在源码根运行） ---------- */
    {
        const gchar *td = "testdata";

        /* INI */
        {
            gchar *p = g_build_filename(td, "sample.ini", NULL);
            LrConfigFile *f = lr_parse_config(p);

            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_INI);
            TEST_ASSERT(f->parsed);
            TEST_ASSERT(f->items->len == 5);

            LrConfigItem *it = g_ptr_array_index(f->items, 0);
            TEST_ASSERT_STR_EQ(it->key, "Port");
            TEST_ASSERT(it->type == LR_VALUE_NUMBER);
            TEST_ASSERT_STR_EQ(it->section, "server");
            TEST_ASSERT_STR_EQ(it->comment, "顶部注释：说明下方配置\n行内注释");

            it = g_ptr_array_index(f->items, 2);
            TEST_ASSERT_STR_EQ(it->key, "Name");
            TEST_ASSERT(it->type == LR_VALUE_STRING);
            TEST_ASSERT_STR_EQ(it->data, "my host");

            it = g_ptr_array_index(f->items, 3);
            TEST_ASSERT_STR_EQ(it->key, "Level");
            TEST_ASSERT_STR_EQ(it->section, "logging");
            TEST_ASSERT_STR_EQ(it->comment, "分号注释：日志配置");

            it = g_ptr_array_index(f->items, 4);
            TEST_ASSERT_STR_EQ(it->key, "Verbose");
            TEST_ASSERT(it->type == LR_VALUE_BOOL);

            lr_config_file_free(f);
            g_free(p);
        }

        /* 扁平 KeyValue */
        {
            gchar *p = g_build_filename(td, "sample.environment.conf", NULL);
            LrConfigFile *f = lr_parse_config(p);

            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_KV);
            TEST_ASSERT(f->parsed);
            TEST_ASSERT(f->items->len == 4);

            LrConfigItem *it = g_ptr_array_index(f->items, 0);
            TEST_ASSERT_STR_EQ(it->key, "PATH");
            TEST_ASSERT(it->type == LR_VALUE_STRING);
            TEST_ASSERT_STR_EQ(it->comment, "系统环境变量风格");

            it = g_ptr_array_index(f->items, 2);
            TEST_ASSERT_STR_EQ(it->key, "DEBUG");
            TEST_ASSERT(it->type == LR_VALUE_BOOL);

            it = g_ptr_array_index(f->items, 3);
            TEST_ASSERT_STR_EQ(it->key, "MAX_CONNECTIONS");
            TEST_ASSERT(it->type == LR_VALUE_NUMBER);

            lr_config_file_free(f);
            g_free(p);
        }

        /* systemd unit */
        {
            gchar *p = g_build_filename(td, "sample.service", NULL);
            LrConfigFile *f = lr_parse_config(p);

            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_SYSTEMD);
            TEST_ASSERT(f->parsed);
            TEST_ASSERT(f->items->len == 6);

            LrConfigItem *it = g_ptr_array_index(f->items, 0);
            TEST_ASSERT_STR_EQ(it->key, "Description");
            TEST_ASSERT_STR_EQ(it->section, "Unit");

            it = g_ptr_array_index(f->items, 4);
            TEST_ASSERT_STR_EQ(it->key, "Restart");
            TEST_ASSERT_STR_EQ(it->data, "on-failure");

            it = g_ptr_array_index(f->items, 5);
            TEST_ASSERT_STR_EQ(it->key, "WantedBy");
            TEST_ASSERT_STR_EQ(it->section, "Install");

            lr_config_file_free(f);
            g_free(p);
        }

        /* 关键字-参数（含被注释配置） */
        {
            gchar *p = g_build_filename(td, "sample.sshd_config", NULL);
            LrConfigFile *f = lr_parse_config(p);

            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_KEYWORD);
            TEST_ASSERT(f->parsed);
            /* 5 项配置 + #UseDNS no 被注释项 = 6 */
            TEST_ASSERT(f->items->len == 6);

            LrConfigItem *it = g_ptr_array_index(f->items, 0);
            TEST_ASSERT_STR_EQ(it->key, "Port");
            TEST_ASSERT(it->type == LR_VALUE_NUMBER);
            TEST_ASSERT_STR_EQ(it->data, "22");
            /* 中文说明文字被当作备注，而非被注释的配置 */
            TEST_ASSERT_STR_EQ(it->comment, "OpenSSH 服务端配置风格");

            it = g_ptr_array_index(f->items, 2);
            TEST_ASSERT_STR_EQ(it->key, "PermitRootLogin");
            TEST_ASSERT(it->type == LR_VALUE_BOOL);

            it = g_ptr_array_index(f->items, 4);
            TEST_ASSERT_STR_EQ(it->key, "UseDNS");
            TEST_ASSERT(!it->enabled);

            it = g_ptr_array_index(f->items, 5);
            TEST_ASSERT_STR_EQ(it->key, "MaxSessions");
            TEST_ASSERT(it->type == LR_VALUE_NUMBER);

            lr_config_file_free(f);
            g_free(p);
        }

        /* JSON */
        {
            gchar *p = g_build_filename(td, "sample.json", NULL);
            LrConfigFile *f = lr_parse_config(p);

            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_JSON);
            TEST_ASSERT(lr_format_supported(LR_FORMAT_JSON));
            TEST_ASSERT(f->parsed);

            lr_config_file_free(f);
            g_free(p);
        }

        /* shebang 脚本：文本兜底 */
        {
            gchar *p = g_build_filename(td, "sample-script.sh", NULL);
            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_UNKNOWN);
            TEST_ASSERT(!lr_format_supported(lr_format_detect(p)));
            g_free(p);
        }

        /* 未知格式普通文本：文本兜底 */
        {
            gchar *p = g_build_filename(td, "sample.unknown.txt", NULL);
            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_UNKNOWN);
            TEST_ASSERT(!lr_format_supported(lr_format_detect(p)));
            g_free(p);
        }

        /* Evolution 数据源：含语言标签键与多节，[ 开头不得误判为 JSON */
        {
            gchar *p = g_build_filename(td, "sample-evolution.source", NULL);
            LrConfigFile *f = lr_parse_config(p);

            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_INI);
            TEST_ASSERT(f->parsed);
            TEST_ASSERT(f->items->len == 9);

            LrConfigItem *it = g_ptr_array_index(f->items, 0);
            TEST_ASSERT_STR_EQ(it->key, "DisplayName");
            TEST_ASSERT_STR_EQ(it->section, "Data Source");

            it = g_ptr_array_index(f->items, 1);
            TEST_ASSERT_STR_EQ(it->key, "DisplayName[zh_CN]");
            TEST_ASSERT_STR_EQ(it->data, "默认代理设置");

            it = g_ptr_array_index(f->items, 4);
            TEST_ASSERT_STR_EQ(it->key, "Method");
            TEST_ASSERT_STR_EQ(it->section, "Proxy");

            it = g_ptr_array_index(f->items, 8);
            TEST_ASSERT_STR_EQ(it->key, "HttpUseAuth");
            TEST_ASSERT(it->type == LR_VALUE_BOOL);

            lr_config_file_free(f);
            g_free(p);
        }

        /* apt 配置：嵌套块 + :: 路径 + 列表值 */
        {
            gchar *p = g_build_filename(td, "sample-apt.conf", NULL);
            LrConfigFile *f = lr_parse_config(p);

            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_APT);
            TEST_ASSERT(f->parsed);
            TEST_ASSERT(f->items->len == 8);

            LrConfigItem *it = g_ptr_array_index(f->items, 0);
            TEST_ASSERT_STR_EQ(it->key, "MetaKey");
            TEST_ASSERT_STR_EQ(it->section,
                               "Acquire::IndexTargets::deb::DEP-11");

            it = g_ptr_array_index(f->items, 2);
            TEST_ASSERT_STR_EQ(it->key, "KeepCompressed");
            TEST_ASSERT(it->type == LR_VALUE_BOOL);

            it = g_ptr_array_index(f->items, 4);
            TEST_ASSERT_STR_EQ(it->section,
                               "Acquire::IndexTargets::deb::DEP-11-icons");

            it = g_ptr_array_index(f->items, 6);
            TEST_ASSERT_STR_EQ(it->key, "DefaultEnabled");
            TEST_ASSERT(it->type == LR_VALUE_BOOL);

            /* 列表值：APT::Update::Post-Invoke-Success { "cmd"; } */
            it = g_ptr_array_index(f->items, 7);
            TEST_ASSERT_STR_EQ(it->key, "[0]");
            TEST_ASSERT_STR_EQ(it->section,
                               "APT::Update::Post-Invoke-Success");
            TEST_ASSERT(it->type == LR_VALUE_STRING);
            TEST_ASSERT(g_strstr_len(it->data, -1, "appstreamcli refresh") !=
                        NULL);

            lr_config_file_free(f);
            g_free(p);
        }

        /* XML：嵌套元素树 + 属性 */
        {
            gchar *p = g_build_filename(td, "sample.xml", NULL);
            LrConfigFile *f = lr_parse_config(p);

            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_XML);
            TEST_ASSERT(f->parsed);
            TEST_ASSERT(f->items->len == 11);

            LrConfigItem *it = g_ptr_array_index(f->items, 0);
            TEST_ASSERT_STR_EQ(it->key, "@version");
            TEST_ASSERT_STR_EQ(it->section, "monitors");
            TEST_ASSERT_STR_EQ(it->data, "2");

            it = g_ptr_array_index(f->items, 1);
            TEST_ASSERT_STR_EQ(it->key, "layoutmode");
            TEST_ASSERT_STR_EQ(it->section, "monitors::configuration");
            TEST_ASSERT_STR_EQ(it->data, "physical");

            it = g_ptr_array_index(f->items, 6);
            TEST_ASSERT_STR_EQ(it->key, "connector");
            TEST_ASSERT_STR_EQ(it->data, "Virtual-1");
            TEST_ASSERT_STR_EQ(it->section,
                               "monitors::configuration::logicalmonitor::"
                               "monitor::monitorspec");

            it = g_ptr_array_index(f->items, 8);
            TEST_ASSERT_STR_EQ(it->key, "width");
            TEST_ASSERT(it->type == LR_VALUE_NUMBER);
            TEST_ASSERT_STR_EQ(it->data, "1814");

            it = g_ptr_array_index(f->items, 10);
            TEST_ASSERT_STR_EQ(it->key, "rate");
            TEST_ASSERT(it->type == LR_VALUE_NUMBER);
            TEST_ASSERT_STR_EQ(it->data, "59.998");

            lr_config_file_free(f);
            g_free(p);
        }

        /* ---------- 解析边界：CRLF / BOM / 引号内 # / 重复节 ---------- */
        {
            gchar *p = write_tmp("lr-edge-1.ini",
                                 "Port = 22\r\nEnable = yes\r\n");
            LrConfigFile *f = lr_parse_config(p);

            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_KV);
            TEST_ASSERT(f->parsed);
            TEST_ASSERT(f->items->len == 2);
            LrConfigItem *it = g_ptr_array_index(f->items, 0);
            TEST_ASSERT_STR_EQ(it->key, "Port");
            TEST_ASSERT_STR_EQ(it->data, "22");

            lr_config_file_free(f);
            g_unlink(p);
            g_free(p);
        }
        {
            gchar *p = write_tmp("lr-edge-2.ini",
                                 "\xEF\xBB\xBF[server]\nPort = 22\n");
            LrConfigFile *f = lr_parse_config(p);

            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_INI);
            TEST_ASSERT(f->parsed);
            TEST_ASSERT(f->items->len == 1);
            LrConfigItem *it = g_ptr_array_index(f->items, 0);
            TEST_ASSERT_STR_EQ(it->section, "server");
            TEST_ASSERT_STR_EQ(it->data, "22");

            lr_config_file_free(f);
            g_unlink(p);
            g_free(p);
        }
        {
            gchar *p = write_tmp("lr-edge-3.conf",
                                 "Name = \"a#b\"   # 行尾注释\n");
            LrConfigFile *f = lr_parse_config(p);

            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_KV);
            TEST_ASSERT(f->parsed);
            TEST_ASSERT(f->items->len == 1);
            LrConfigItem *it = g_ptr_array_index(f->items, 0);
            TEST_ASSERT_STR_EQ(it->data, "a#b");
            TEST_ASSERT_STR_EQ(it->comment, "行尾注释");

            lr_config_file_free(f);
            g_unlink(p);
            g_free(p);
        }
        {
            gchar *p = write_tmp("lr-edge-4.ini",
                                 "[s]\na = 1\n[s]\nb = 2\n");
            LrConfigFile *f = lr_parse_config(p);

            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_INI);
            TEST_ASSERT(f->parsed);
            TEST_ASSERT(f->items->len == 2);
            LrConfigItem *it0 = g_ptr_array_index(f->items, 0);
            LrConfigItem *it1 = g_ptr_array_index(f->items, 1);
            TEST_ASSERT_STR_EQ(it0->section, "s");
            TEST_ASSERT_STR_EQ(it1->section, "s");

            lr_config_file_free(f);
            g_unlink(p);
            g_free(p);
        }

        /* ---------- 空文件 / 仅注释：不得误判为配置 ---------- */
        {
            gchar *p = write_tmp("lr-edge-5.conf", "");
            LrConfigFile *f = lr_parse_config(p);

            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_UNKNOWN);
            TEST_ASSERT(!f->parsed);
            TEST_ASSERT(f->items->len == 0);

            lr_config_file_free(f);
            g_unlink(p);
            g_free(p);
        }
        {
            gchar *p = write_tmp("lr-edge-6.conf",
                                 "# 只有注释\n; 另一行\n");
            LrConfigFile *f = lr_parse_config(p);

            TEST_ASSERT(lr_format_detect(p) == LR_FORMAT_UNKNOWN);
            TEST_ASSERT(!f->parsed);

            lr_config_file_free(f);
            g_unlink(p);
            g_free(p);
        }

        /* ---------- 后缀优先级：相同内容 .toml / .ini ---------- */
        {
            const gchar *body = "[x]\nkey = \"v\"\n";
            gchar *pa = write_tmp("lr-pri.toml", body);
            gchar *pb = write_tmp("lr-pri.ini", body);

            TEST_ASSERT(lr_format_detect(pa) == LR_FORMAT_TOML);
            TEST_ASSERT(lr_format_detect(pb) == LR_FORMAT_INI);

            g_unlink(pa);
            g_unlink(pb);
            g_free(pa);
            g_free(pb);
        }
    }
}
