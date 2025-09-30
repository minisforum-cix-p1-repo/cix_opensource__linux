/**************************************************************************************/
/*                          COPYRIGHT INFORMATION                                     */
/*     Copyright 2024 Cix Technology Group Co., Ltd.                             */
/*     All Rights Reserved.                                                           */
/*                                                                                    */
/*     The following programs are the sole property of Copyright 2024 Cix Technology Group Co., Ltd.     */
/*     Co., Ltd., and contain its proprietary and confidential information.           */
/*                                                                                    */
/*                                                                                    */
/**************************************************************************************/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/poll.h>
#include <linux/bitmap.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <uapi/linux/sched/types.h>
#include <linux/sched_clock.h>
#include <linux/log2.h>
#include "hf_manager.h"

#define hf_debug(fmt, ...) \
		printk_deferred(KERN_INFO "[hf_core][%s]"fmt, __func__, ##__VA_ARGS__)
#define print_s64(l) (((l) == S64_MAX) ? -1 : (l))

static int major;
static struct class *hf_manager_class;

static DECLARE_BITMAP(sensor_list_bitmap, SENSOR_TYPE_SENSOR_MAX);
static struct hf_core hfcore;
static struct task_struct *task;

static void init_hf_core(struct hf_core *core)
{
	int i = 0;
	mutex_init(&core->manager_lock);
	INIT_LIST_HEAD(&core->manager_list);
	for (i = 0; i < SENSOR_TYPE_SENSOR_MAX; ++i) {
		core->state[i].delay = S64_MAX;
		core->state[i].latency = S64_MAX;
		core->state[i].start_time = S64_MAX;
	}
	spin_lock_init(&core->client_lock);
	INIT_LIST_HEAD(&core->client_list);

	kthread_init_worker(&core->kworker);

}

static struct hf_manager *hf_manager_find_manager(struct hf_core *core,
		uint8_t sensor_type)
{
	int i = 0;
	struct hf_manager *manager = NULL;
	struct hf_device *device = NULL;
	list_for_each_entry(manager, &core->manager_list, list) {
		device = READ_ONCE(manager->hf_dev);
		if (!device || !device->support_list)
			continue;
		for (i = 0; i < device->support_size; ++i) {
			if (sensor_type == device->support_list[i].sensor_type)
				return manager;
		}
	}
	pr_err("Failed to find manager, %u unregistered\n", sensor_type);
	return NULL;
}

static inline void hf_manager_save_update_enable(struct hf_client *client,
		struct hf_manager_cmd *cmd, struct sensor_state *old)
{
	unsigned long flags;
	struct hf_manager_batch *batch = (struct hf_manager_batch *)cmd->data;
	struct sensor_state *request = &client->request[cmd->sensor_type];
	spin_lock_irqsave(&client->core->client_lock, flags);
	/* only enable disable update action delay and latency */
	if (cmd->action == HF_MANAGER_SENSOR_ENABLE) {
		/*
		 * NOTE: save significant parameter to old
		 * remember mustn't save flush bias raw etc
		 * down_sample_cnt and down_sample_div mustn't save due to
		 * own_sample_update called in hf_manager_device_enable
		 * when enable disable and batch device success
		 */
		old->enable = request->enable;
		old->down_sample = request->down_sample;
		old->delay = request->delay;
		old->latency = request->latency;
		old->start_time = request->start_time;
		/* update new */
		if (!request->enable)
			//request->start_time = ktime_get_ns();
			request->start_time = 0;
		request->enable = true;
		request->down_sample = cmd->down_sample;
		request->delay = batch->delay;
		request->latency = batch->latency;
	} else if (cmd->action == HF_MANAGER_SENSOR_DISABLE) {
		request->enable = false;
		request->down_sample = false;
		request->delay = S64_MAX;
		request->latency = S64_MAX;
		request->start_time = S64_MAX;
	}
	spin_unlock_irqrestore(&client->core->client_lock, flags);
}

static inline void hf_manager_restore_enable(struct hf_client *client,
		struct hf_manager_cmd *cmd, struct sensor_state *old)
{
	unsigned long flags;
	struct sensor_state *request = &client->request[cmd->sensor_type];
	spin_lock_irqsave(&client->core->client_lock, flags);
	if (cmd->action == HF_MANAGER_SENSOR_ENABLE) {
		/*
		 * NOTE: restore significant parameter from old
		 * remember mustn't restore flush bias raw etc
		 * down_sample_cnt and down_sample_div mustn't restore due to
		 * down_sample_update called in hf_manager_device_enable
		 * when enable disable and batch device success
		 */
		request->enable = old->enable;
		request->down_sample = old->down_sample;
		request->delay = old->delay;
		request->latency = old->latency;
		request->start_time = old->start_time;
	} else if (cmd->action == HF_MANAGER_SENSOR_DISABLE) {
		request->enable = false;
		request->down_sample = false;
		request->delay = S64_MAX;
		request->latency = S64_MAX;
		request->start_time = S64_MAX;
	}
	spin_unlock_irqrestore(&client->core->client_lock, flags);
}

static inline void hf_manager_inc_flush(struct hf_client *client,
		uint8_t sensor_type)
{
	unsigned long flags;
	spin_lock_irqsave(&client->core->client_lock, flags);
	client->request[sensor_type].flush++;
	spin_unlock_irqrestore(&client->core->client_lock, flags);
}

static inline void hf_manager_dec_flush(struct hf_client *client,
		uint8_t sensor_type)
{
	unsigned long flags;
	spin_lock_irqsave(&client->core->client_lock, flags);
	if (client->request[sensor_type].flush > 0)
		client->request[sensor_type].flush--;
	spin_unlock_irqrestore(&client->core->client_lock, flags);
}

static inline void hf_manager_update_raw(struct hf_client *client,
		uint8_t sensor_type, bool enable)
{
	unsigned long flags;
	spin_lock_irqsave(&client->core->client_lock, flags);
	client->request[sensor_type].raw = enable;
	spin_unlock_irqrestore(&client->core->client_lock, flags);
}

static inline void hf_manager_clear_raw(struct hf_client *client,
		uint8_t sensor_type)
{
	unsigned long flags;
	spin_lock_irqsave(&client->core->client_lock, flags);
	client->request[sensor_type].raw = false;
	spin_unlock_irqrestore(&client->core->client_lock, flags);
}

static int hf_manager_device_enable(struct hf_device *device,
		uint8_t sensor_type)
{
	if (!device->enable || !device->batch)
		return -EINVAL;
	/* TBD */
	return 0;
}

static int hf_manager_device_flush(struct hf_device *device,
		uint8_t sensor_type)
{
	if (!device->flush)
		return -EINVAL;
	return device->flush(device, sensor_type);
}

static int hf_manager_device_calibration(struct hf_device *device,
		uint8_t sensor_type)
{
	if (device->calibration)
		return device->calibration(device, sensor_type);
	return 0;
}

static int hf_manager_device_config_cali(struct hf_device *device,
		uint8_t sensor_type, void *data, uint8_t length)
{
	if (device->config_cali)
		return device->config_cali(device, sensor_type, data, length);
	return 0;
}

static int hf_manager_device_selftest(struct hf_device *device,
		uint8_t sensor_type)
{
	if (device->selftest)
		return device->selftest(device, sensor_type);
	return 0;
}

static int hf_manager_device_rawdata(struct hf_device *device,
		uint8_t sensor_type)
{
	int err = 0;
	unsigned long flags;
	struct hf_core *core = device->manager->core;
	struct hf_client *client = NULL;
	struct sensor_state *request = NULL;
	bool best_enable = false;
	if (!device->rawdata)
		return 0;
	spin_lock_irqsave(&core->client_lock, flags);
	list_for_each_entry(client, &core->client_list, list) {
		request = &client->request[sensor_type];
		if (request->raw)
			best_enable = true;
	}
	spin_unlock_irqrestore(&core->client_lock, flags);
	if (core->state[sensor_type].raw == best_enable)
		return 0;
	core->state[sensor_type].raw = best_enable;
	err = device->rawdata(device, sensor_type, best_enable);
	if (err < 0)
		core->state[sensor_type].raw = false;
	return err;
}

static int hf_manager_drive_device(struct hf_client *client,
		struct hf_manager_cmd *cmd)
{
	int err = 0;
	struct sensor_state old;
	struct hf_manager *manager = NULL;
	struct hf_device *device = NULL;
	struct hf_core *core = client->core;
	uint8_t sensor_type = cmd->sensor_type;
	if (unlikely(sensor_type >= SENSOR_TYPE_SENSOR_MAX))
		return -EINVAL;
	mutex_lock(&core->manager_lock);
	manager = hf_manager_find_manager(core, sensor_type);
	if (!manager) {
		mutex_unlock(&core->manager_lock);
		return -EINVAL;
	}
	device = manager->hf_dev;
	if (!device || !device->dev_name) {
		mutex_unlock(&core->manager_lock);
		return -EINVAL;
	}
	switch (cmd->action)
	{
		case HF_MANAGER_SENSOR_ENABLE:
		case HF_MANAGER_SENSOR_DISABLE:
			hf_manager_save_update_enable(client, cmd, &old);
			err = hf_manager_device_enable(device, sensor_type);
			if (err < 0)
				hf_manager_restore_enable(client, cmd, &old);
			break;
		case HF_MANAGER_SENSOR_FLUSH:
			hf_manager_inc_flush(client, sensor_type);
			err = hf_manager_device_flush(device, sensor_type);
			if (err < 0)
				hf_manager_dec_flush(client, sensor_type);
			break;
		case HF_MANAGER_SENSOR_ENABLE_CALI:
			err = hf_manager_device_calibration(device, sensor_type);
			break;
		case HF_MANAGER_SENSOR_CONFIG_CALI:
			err = hf_manager_device_config_cali(device,
						sensor_type, cmd->data, cmd->length);
			break;
		case HF_MANAGER_SENSOR_SELFTEST:
			err = hf_manager_device_selftest(device, sensor_type);
			break;
		case HF_MANAGER_SENSOR_RAWDATA:
			hf_manager_update_raw(client, sensor_type, cmd->data[0]);
			err = hf_manager_device_rawdata(device, sensor_type);
			if (err < 0)
				hf_manager_clear_raw(client, sensor_type);
			break;
		default:
			pr_err("Unknown action %u\n", cmd->action);
			err = -EINVAL;
			break;
	}
	mutex_unlock(&core->manager_lock);
	return err;
}

static int hf_manager_device_info(struct hf_client *client,
		uint8_t sensor_type, struct sensor_info *info)
{
	int i = 0;
	int ret = 0;
	struct hf_manager *manager = NULL;
	struct hf_device *device = NULL;
	struct sensor_info *si = NULL;
	mutex_lock(&client->core->manager_lock);
	manager = hf_manager_find_manager(client->core, sensor_type);
	if (!manager) {
		ret = -EINVAL;
		goto err_out;
	}
	device = manager->hf_dev;
	if (!device || !device->support_list ||
			!device->support_size) {
		ret = -EINVAL;
		goto err_out;
	}
	for (i = 0; i < device->support_size; ++i) {
		if (device->support_list[i].sensor_type == sensor_type) {
			si = &device->support_list[i];
			break;
		}
	}
	if (!si) {
		ret = -EINVAL;
		goto err_out;
	}
	*info  = *si;
err_out:
	mutex_unlock(&client->core->manager_lock);
	return ret;
}

static int hf_manager_get_sensor_info(struct hf_client *client,
		uint8_t sensor_type, struct sensor_info *info)
{
	return hf_manager_device_info(client, sensor_type, info);
}

struct hf_client *hf_client_create(void)
{
	unsigned long flags;
	struct hf_client *client = NULL;
	struct hf_client_fifo *hf_fifo = NULL;
	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		goto err_out;
	/* record process id and thread id for debug */
	strlcpy(client->proc_comm, current->comm, sizeof(client->proc_comm));
	client->leader_pid = current->group_leader->pid;
	client->pid = current->pid;
	client->core = &hfcore;
	INIT_LIST_HEAD(&client->list);
	hf_fifo = &client->hf_fifo;
	hf_fifo->head = 0;
	hf_fifo->tail = 0;
	hf_fifo->bufsize = roundup_pow_of_two(HF_CLIENT_FIFO_SIZE);
	hf_fifo->buffull = false;
	spin_lock_init(&hf_fifo->buffer_lock);
	init_waitqueue_head(&hf_fifo->wait);
	hf_fifo->buffer =
		kcalloc(hf_fifo->bufsize, sizeof(*hf_fifo->buffer),
			GFP_KERNEL);
	if (!hf_fifo->buffer)
		goto err_free;
	spin_lock_init(&client->request_lock);
	spin_lock_irqsave(&client->core->client_lock, flags);
	list_add(&client->list, &client->core->client_list);
	spin_unlock_irqrestore(&client->core->client_lock, flags);
	return client;
err_free:
	kfree(client);
err_out:
	return NULL;
}
EXPORT_SYMBOL_GPL(hf_client_create);

void hf_client_destroy(struct hf_client *client)
{
	unsigned long flags;
	spin_lock_irqsave(&client->core->client_lock, flags);
	list_del(&client->list);
	spin_unlock_irqrestore(&client->core->client_lock, flags);
	kfree(client->hf_fifo.buffer);
	kfree(client);
}
EXPORT_SYMBOL_GPL(hf_client_destroy);

static int fetch_next(struct hf_client_fifo *hf_fifo,
				  struct hf_manager_event *event)
{
	unsigned long flags;
	int have_event;
	spin_lock_irqsave(&hf_fifo->buffer_lock, flags);
	have_event = hf_fifo->head != hf_fifo->tail;
	if (have_event) {
		*event = hf_fifo->buffer[hf_fifo->tail++];
		hf_fifo->tail &= hf_fifo->bufsize - 1;
		hf_fifo->buffull = false;
		// hf_fifo->client_active = ktime_get_ns();
		hf_fifo->client_active = 0;
	}
	spin_unlock_irqrestore(&hf_fifo->buffer_lock, flags);
	return have_event;
}

static int hf_manager_open(struct inode *inode, struct file *filp)
{
	struct hf_client *client = hf_client_create();
	if (!client)
		return -ENOMEM;
	filp->private_data = client;
	nonseekable_open(inode, filp);
	return 0;
}

static int hf_manager_release(struct inode *inode, struct file *filp)
{
	struct hf_client *client = filp->private_data;
	filp->private_data = NULL;
	hf_client_destroy(client);
	return 0;
}

static ssize_t hf_manager_read(struct file *filp,
		char __user *buf, size_t count, loff_t *f_pos)
{
	struct hf_client *client = filp->private_data;
	struct hf_client_fifo *hf_fifo = &client->hf_fifo;
	struct hf_manager_event event;
	size_t read = 0;
	if (count != 0 && count < sizeof(struct hf_manager_event))
		return -EINVAL;
	for (;;) {
		if (hf_fifo->head == hf_fifo->tail)
			return 0;
		if (count == 0)
			break;
		while (read + sizeof(event) <= count &&
				fetch_next(hf_fifo, &event)) {
			if (copy_to_user(buf + read, &event, sizeof(event)))
				return -EFAULT;
			read += sizeof(event);
		}
		if (read)
			break;
	}
	return read;
}

static ssize_t hf_manager_write(struct file *filp,
		const char __user *buf, size_t count, loff_t *f_ops)
{
	struct hf_manager_cmd cmd;
	struct hf_client *client = filp->private_data;
	memset(&cmd, 0, sizeof(cmd));
	if (count != sizeof(struct hf_manager_cmd))
		return -EINVAL;
	if (copy_from_user(&cmd, buf, count))
		return -EFAULT;
	return hf_manager_drive_device(client, &cmd);
}

static unsigned int hf_manager_poll(struct file *filp,
		struct poll_table_struct *wait)
{
	struct hf_client *client = filp->private_data;
	struct hf_client_fifo *hf_fifo = &client->hf_fifo;
	unsigned int mask = 0;
	client->ppid = current->pid;
	poll_wait(filp, &hf_fifo->wait, wait);
	if (hf_fifo->head != hf_fifo->tail)
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

static long hf_manager_ioctl(struct file *filp,
			unsigned int cmd, unsigned long arg)
{
	struct hf_client *client = filp->private_data;
	unsigned int size = _IOC_SIZE(cmd);
	void __user *ubuf = (void __user *)arg;
	uint8_t sensor_type = 0;
	struct ioctl_packet packet;
	struct sensor_info info;
	struct hf_device *device = NULL;
	memset(&packet, 0, sizeof(packet));
	if (size != sizeof(struct ioctl_packet))
		return -EINVAL;
	if (copy_from_user(&packet, ubuf, sizeof(packet)))
		return -EFAULT;
	sensor_type = packet.sensor_type;
	if (unlikely(sensor_type >= SENSOR_TYPE_SENSOR_MAX))
		return -EINVAL;
	switch (cmd)
	{
		case HF_MANAGER_REQUEST_REGISTER_STATUS:
			/* TBD */
			break;
		case HF_MANAGER_REQUEST_BIAS_DATA:
			/* TBD */
			break;
		case HF_MANAGER_REQUEST_CALI_DATA:
			/* TBD */
			break;
		case HF_MANAGER_REQUEST_SENSOR_INFO:
			if (!test_bit(sensor_type, sensor_list_bitmap))
				return -EINVAL;
			memset(&info, 0, sizeof(info));
			if (hf_manager_get_sensor_info(client, sensor_type, &info))
				return -EINVAL;
			if (sizeof(packet.byte) < sizeof(info))
				return -EINVAL;
			memcpy(packet.byte, &info, sizeof(info));
			if (copy_to_user(ubuf, &packet, sizeof(packet)))
				return -EFAULT;
			break;
		case HF_MANAGER_REQUEST_READY_STATUS:
			mutex_lock(&client->core->device_lock);
			packet.status = true;
			list_for_each_entry(device, &client->core->device_list, list) {
				if (!READ_ONCE(device->ready)) {
					pr_err_ratelimited("Device:%s not ready\n",
							device->dev_name);
					packet.status = false;
					break;
				}
			}
			mutex_unlock(&client->core->device_lock);
			if (copy_to_user(ubuf, &packet, sizeof(packet)))
				return -EFAULT;
			break;
		default:
			pr_err("Unknown command %u\n", cmd);
			return -EINVAL;
	}
	return 0;
}

static const struct file_operations hf_manager_fops = {
	.owner			= THIS_MODULE,
	.open			= hf_manager_open,
	.release		= hf_manager_release,
	.read			= hf_manager_read,
	.write			= hf_manager_write,
	.poll			= hf_manager_poll,
	.unlocked_ioctl	= hf_manager_ioctl,
	.compat_ioctl	= hf_manager_ioctl,
};

static int hf_manager_proc_show(struct seq_file *m, void *v)
{
	int i = 0, j = 0, k = 0;
	uint8_t sensor_type = 0;
	unsigned long flags;
	struct hf_core *core = (struct hf_core *)m->private;
	struct hf_manager *manager = NULL;
	struct hf_client *client = NULL;
	struct hf_device *device = NULL;
	seq_puts(m, "**************************************************\n");
	seq_puts(m, "Manager List:\n");
	mutex_lock(&core->manager_lock);
	j = 1;
	k = 1;
	list_for_each_entry(manager, &core->manager_list, list) {
		device = READ_ONCE(manager->hf_dev);
		if (!device || !device->support_list)
			continue;
		seq_printf(m, "%d. manager:[%d,%lld]\n", j++,
			atomic_read(&manager->io_enabled),
			print_s64((int64_t)atomic64_read(
				&manager->io_poll_interval)));
		seq_printf(m, " device:%s poll:%s bus:%s online\n",
			device->dev_name,
			device->device_poll ? "io_polling" : "io_interrupt",
			device->device_bus ? "io_async" : "io_sync");
		for (i = 0; i < device->support_size; ++i) {
			sensor_type = device->support_list[i].sensor_type;
			seq_printf(m, "  (%d) type:%u info:[%u,%s,%s]\n",
				k++,
				sensor_type,
				device->support_list[i].gain,
				device->support_list[i].name,
				device->support_list[i].vendor);
		}
	}
	mutex_unlock(&core->manager_lock);
	seq_puts(m, "**************************************************\n");
	seq_puts(m, "Client List:\n");
	spin_lock_irqsave(&core->client_lock, flags);
	j = 1;
	k = 1;
	list_for_each_entry(client, &core->client_list, list) {
		seq_printf(m, "%d. client:%s pid:[%d:%d,%d] online\n",
			j++,
			client->proc_comm,
			client->leader_pid,
			client->pid,
			client->ppid);
		for (i = 0; i < SENSOR_TYPE_SENSOR_MAX; ++i) {
			if (!client->request[i].enable)
				continue;
			seq_printf(m, " (%d) type:%d param:[%lld,%lld,%lld]",
				k++,
				i,
				client->request[i].delay,
				client->request[i].latency,
				client->request[i].start_time);
			seq_printf(m, " ds:[%u,%u,%u]\n",
				client->request[i].down_sample,
				client->request[i].down_sample_cnt,
				client->request[i].down_sample_div);
		}
	}
	spin_unlock_irqrestore(&core->client_lock, flags);
	seq_puts(m, "**************************************************\n");
	seq_puts(m, "Active List:\n");
	mutex_lock(&core->manager_lock);
	j = 1;
	for (i = 0; i < SENSOR_TYPE_SENSOR_MAX; ++i) {
		if (!core->state[i].enable)
			continue;
		seq_printf(m, "%d. type:%d param:[%lld,%lld]\n",
			j++,
			i,
			core->state[i].delay,
			core->state[i].latency);
	}
	mutex_unlock(&core->manager_lock);
	return 0;
}

static int hf_manager_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, hf_manager_proc_show, pde_data(inode));
}

static const struct proc_ops hf_manager_proc_fops = {
	.proc_open		= hf_manager_proc_open,
	.proc_release	= single_release,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
};

static int __init hf_manager_init(void)
{
	int ret = 0;
    struct device *dev;
	//struct sched_param param = { .sched_priority = MAX_RT_PRIO / 2 };

	init_hf_core(&hfcore);

	major = register_chrdev(0, "hf_manager", &hf_manager_fops);
	if (major < 0) {
		pr_err("Unable to get major\n");
		ret = major;
		goto err_exit;
	}

	hf_manager_class = class_create("hf_manager");
	if (IS_ERR(hf_manager_class)) {
		pr_err("Failed to create class\n");
		ret = PTR_ERR(hf_manager_class);
		goto err_chredev;
	}

	dev = device_create(hf_manager_class, NULL, MKDEV(major, 0),
		NULL, "hf_manager");
	if (IS_ERR(dev)) {
		pr_err("Failed to create device\n");
		ret = PTR_ERR(dev);
		goto err_class;
	}

	if (!proc_create_data("hf_manager", 0440, NULL,
			&hf_manager_proc_fops, &hfcore))
		pr_err("Failed to create proc\n");

	task = kthread_run(kthread_worker_fn,
			&hfcore.kworker, "hf_manager");
	if (IS_ERR(task)) {
		pr_err("Failed to create kthread\n");
		ret = PTR_ERR(task);
		goto err_device;
	}

	sched_set_fifo(task);
	return 0;

err_device:
	device_destroy(hf_manager_class, MKDEV(major, 0));
err_class:
	class_destroy(hf_manager_class);
err_chredev:
	unregister_chrdev(major, "hf_manager");
err_exit:
	return ret;
}

static void __exit hf_manager_exit(void)
{
	kthread_stop(task);
	device_destroy(hf_manager_class, MKDEV(major, 0));
	class_destroy(hf_manager_class);
	unregister_chrdev(major, "hf_manager");
}

module_init(hf_manager_init);
module_exit(hf_manager_exit);

MODULE_DESCRIPTION("cix high frequency manager");
MODULE_LICENSE("GPL V2");
