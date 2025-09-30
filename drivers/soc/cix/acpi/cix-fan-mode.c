// SPDX-License-Identifier: GPL-2.0
/*
 * fan driver for the cix ec
 *
 * Copyright 2024 Cix Technology Group Co., Ltd..
 */

#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define MODE_NAME_LEN			16
#define MODE_OFFSET			2

enum {
	MUTE_MODE,
	NORMAL_MODE,
	PERF_MODE,
	MAX_MODE,
};

struct cix_fan_mode_data {
	struct kobject *cix_kobj;
	int mode;
};

struct cix_fan_mode_data cix_fan_mdata;

static char *fan_available_mode[] = {
	"mute",
	"normal",
	"performance"
};

static char *fan_method[] = {
	"SFMT", // mute mode
	"SFAT", // audo mode
	"SFPF", // performance mode
};

static int cix_has_fan_control_device(void)
{
	acpi_status status;
	acpi_handle handle;

	status = acpi_get_handle(NULL, "\\_SB.HWMN", &handle);
	if (ACPI_FAILURE(status))
		return 0;

	return 1;
}

static int cix_set_fan_mode(struct device *dev, int mode)
{
	acpi_status status;
	acpi_handle handle;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	if (mode >= MAX_MODE)
		return -EINVAL;

	status = acpi_get_handle(NULL, "\\_SB.HWMN", &handle);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	status = acpi_evaluate_object(handle, fan_method[mode], NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		pr_err("set fan mode failed: %s\n",
					acpi_format_exception(status));
		return -EINVAL;
	}
	kfree(buffer.pointer);

	return 0;
}

static ssize_t mode_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct cix_fan_mode_data *fd = &cix_fan_mdata;

	if (fd->mode < 0 || fd->mode >= MAX_MODE)
		return -EINVAL;

	return sprintf(buf, "%s\n", fan_available_mode[fd->mode]);
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct cix_fan_mode_data *fd = &cix_fan_mdata;
	char mode[MODE_NAME_LEN + 1];
	int ret;
	int i;

	ret = sscanf(buf, "%s", mode);
	if (ret != 1)
		return -EINVAL;

	for (i = 0; i < MAX_MODE; i++) {
		if (!strcmp(mode, fan_available_mode[i]))
			break;
	}

	if (i < MAX_MODE) {
		fd->mode = i;
		ret = cix_set_fan_mode(dev, fd->mode);
		if (ret < 0)
			pr_err("Failed: set fan mode!\n");
	}

	return count;
}
static DEVICE_ATTR_RW(mode);

static ssize_t available_mode_show(struct device *d,
					struct device_attribute *attr,
					char *buf)
{
	ssize_t count = 0;
	int i;

	for (i = 0; i < MAX_MODE; i++) {
		count += scnprintf(&buf[count], (PAGE_SIZE - count - 2),
				"%s ", fan_available_mode[i]);
	}

	/* Truncate the trailing space */
	if (count)
		count--;

	count += sprintf(&buf[count], "\n");

	return count;
}
static DEVICE_ATTR_RO(available_mode);

#define CREATE_SYSFS_FILE(kobj, name)					\
{									\
	int ret;							\
	ret = sysfs_create_file(kobj, &dev_attr_##name.attr);		\
	if (ret < 0) {							\
		pr_warn("Unable to create attr(%s)\n", "##name");	\
	}								\
}									\

static int __init cix_fan_mode_init(void)
{
	struct cix_fan_mode_data *fd = &cix_fan_mdata;

	if (!cix_has_fan_control_device())
		return 0;

	fd->mode = NORMAL_MODE;
	fd->cix_kobj = kobject_create_and_add("cix_fan", NULL);
	if (!fd->cix_kobj)
		return -ENOMEM;
	CREATE_SYSFS_FILE(fd->cix_kobj, mode);
	CREATE_SYSFS_FILE(fd->cix_kobj, available_mode);

	return 0;
}
module_init(cix_fan_mode_init);

static void __exit cix_fan_mode_exit(void)
{
	struct cix_fan_mode_data *fd = &cix_fan_mdata;

	sysfs_remove_file(fd->cix_kobj, &dev_attr_mode.attr);
	sysfs_remove_file(fd->cix_kobj, &dev_attr_available_mode.attr);
	kobject_put(fd->cix_kobj);
}
module_exit(cix_fan_mode_exit);

MODULE_ALIAS("platform:cix-fan-mode");
MODULE_DESCRIPTION("CIX Fan Mode");
MODULE_LICENSE("GPL v2");
