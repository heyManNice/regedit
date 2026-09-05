/* 测试用根目录开关：通过环境变量覆盖 /etc、~/.config、/boot，
 * 使 UI 回归测试可以指向固定的 fixture 目录。 */
#ifndef LR_UI_TEST_ROOTS_H
#define LR_UI_TEST_ROOTS_H

const char *lr_etc_root(void);
const char *lr_config_root(void);
const char *lr_boot_root(void);

#endif /* LR_UI_TEST_ROOTS_H */
