#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#include "tg_protocol.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zchvma");
MODULE_DESCRIPTION("Telegram FS Kernel Module - Step 2");

static int major_num;
static struct class *tg_class; // Не, ну это ржака, кнчн... Когда я это увидел, чуть не помер, лол.
static struct cdev ctrl_cdev;
static struct cdev chat_cdev;

DECLARE_WAIT_QUEUE_HEAD(server_wait_queue); // Здесь спит сервер, ожидая запросов
DECLARE_WAIT_QUEUE_HEAD(client_wait_queue); // Здесь спит утилита, ожидая ответа от сервера

static struct telegram_message current_request;
static int request_pending = 0; // 1, если есть запрос для сервера
static int response_ready = 0;  // 1, если сервер подготовил ответ

DEFINE_MUTEX(transaction_mutex);

static ssize_t ctrl_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
	if (wait_event_interruptible(server_wait_queue, request_pending == 1)) {
		return -ERESTARTSYS;
	}

	printk(KERN_INFO "[TG_FS] Получен запрос на сервер (Тип: %d, Чат: %d)\n", current_request.type, current_request.chat_id);

	if (copy_to_user(buf, &current_request, sizeof(struct telegram_message))) {
		request_pending = 0;
	        response_ready = -1;
        	wake_up_interruptible(&client_wait_queue);
		return -EFAULT;
	}

	request_pending = 0;

	return sizeof(struct telegram_message);
}

static ssize_t ctrl_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
	if (copy_from_user(&current_request, buf, sizeof(struct telegram_message))) {
		response_ready = -1;
	        wake_up_interruptible(&client_wait_queue);
		return -EFAULT;
	}

	printk(KERN_INFO "Получен запрос на сервер (Тип: %d, Чат: %d)\n", current_request.type, current_request.chat_id);

	response_ready = 1;

	wake_up_interruptible(&client_wait_queue);

	return count;
}

// cat или открытие файл на запись
static int chat_open(struct inode *inode, struct file *file) {
	printk(KERN_INFO "[TG_FS] chat_open: Кто-то открыл чат!\n");
	return 0;
}

// Закрытие файла
static int chat_release(struct inode *inode, struct file *file) {
	printk(KERN_INFO "[TG_FS] chat_release: Чат закрыт.\n");
	return 0;
}

// Попытка чтения (cat)
static ssize_t chat_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
	int chat_id;
    	int len;

	if (*ppos > 0) {
		return 0;
	}

	chat_id = iminor(file_inode(file));

	printk(KERN_INFO "[TG_FS] Попытка чтения (Чат: %d)\n", chat_id);

	mutex_lock(&transaction_mutex);

	current_request.type = Read;
	current_request.chat_id = chat_id;
	request_pending = 1;
	response_ready = 0;

	wake_up_interruptible(&server_wait_queue);

	while (1) {
	       	int ret = wait_event_interruptible(client_wait_queue, response_ready != 0);

	        // Обработка Ctrl+C
	        if (ret == -ERESTARTSYS) {
			if (request_pending == 1) {
				// Сервер еще спал. Безопасно отменяем запрос.
				request_pending = 0;
				mutex_unlock(&transaction_mutex);
				return -ERESTARTSYS;
			} else {
				// Запрос уже обрабатывается сервером. Ждем принудительно, чтобы не сломать чужие транзакции.
				wait_event(client_wait_queue, response_ready != 0);
			}
		}
		break;
	}

	// Проверка, не сломался ли сервер в процессе
	if (response_ready == -1) {
		response_ready = 0;
		mutex_unlock(&transaction_mutex);
		return -EIO;
	}

	len = current_request.length;

	if (len < 0) {
		len = 0;
    	}

	if (len > MAX_BUFFER_SIZE - 1) {
		len = MAX_BUFFER_SIZE - 1;
	}

	if (len > count) {
		len = count;
	}

	if (copy_to_user(buf, current_request.buffer, len)) {
		mutex_unlock(&transaction_mutex);
		return -EFAULT;
	}

	*ppos += len;
	response_ready = 0;
	mutex_unlock(&transaction_mutex);

	return len;
}

// Попытка записи (echo)
static ssize_t chat_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
	int chat_id = iminor(file_inode(file));
	int len = count;

	printk(KERN_INFO "[TG_FS] Попытка записи (Чат: %d)\n", chat_id);

	if (len < 0) {
		len = 0;
	}

	if (len >= MAX_BUFFER_SIZE) {
		len = MAX_BUFFER_SIZE - 1;
	}

	mutex_lock(&transaction_mutex);

	current_request.type = Write;
	current_request.chat_id = chat_id;

	if (copy_from_user(current_request.buffer, buf, len)) {
		mutex_unlock(&transaction_mutex);
		return -EFAULT;
	}

	current_request.buffer[len] = '\0'; 
	current_request.length = len;
	request_pending = 1;
	response_ready = 0;

	wake_up_interruptible(&server_wait_queue);

        while (1) {
                int ret = wait_event_interruptible(client_wait_queue, response_ready != 0);

                // Обработка Ctrl+C
                if (ret == -ERESTARTSYS) {
                        if (request_pending == 1) {
                                // Сервер еще спал. Безопасно отменяем запрос.
                                request_pending = 0;
                                mutex_unlock(&transaction_mutex);
                                return -ERESTARTSYS;
                        } else {
                                // Запрос уже обрабатывается сервером. Ждем принудительно, чтобы не сломать чужие транзакции.
                                wait_event(client_wait_queue, response_ready != 0);
                        }
                }
                break;
        }

        // Проверка, не сломался ли сервер в процессе
        if (response_ready == -1) {
                response_ready = 0;
                mutex_unlock(&transaction_mutex);
                return -EIO;
        }


	response_ready = 0;
	mutex_unlock(&transaction_mutex);

	return count;
}

static struct file_operations ctrl_fops = {
    .owner = THIS_MODULE,
    .read = ctrl_read,
    .write = ctrl_write,
};

static struct file_operations chat_fops = {
    .owner = THIS_MODULE,
    .open = chat_open,
    .release = chat_release,
    .read = chat_read,
    .write = chat_write,
};

static int __init tg_init(void) {
	dev_t dev_num;
	int num_devs = NUM_CHATS + 1;

	if (alloc_chrdev_region(&dev_num, 0, num_devs, "telegram") < 0) {
		goto err_message;
	}

	major_num = MAJOR(dev_num);

	tg_class = class_create("telegram_class"); // /sys/class/telegram_class/
	if (IS_ERR(tg_class)) {
		unregister_chrdev_region(dev_num, num_devs);
		printk(KERN_ALERT "[TG_FS] Ошибка инициализации, откат выполнен.\n");
		return PTR_ERR(tg_class);
	}

	cdev_init(&ctrl_cdev, &ctrl_fops);
	if (cdev_add(&ctrl_cdev, MKDEV(major_num, 0), 1) < 0) {
	        goto err_class;
    	}

	// /dev/telegram/
        // Минор 0 — для сервера 
	if (IS_ERR(device_create(tg_class, NULL, MKDEV(major_num, 0), NULL, "telegram/ctrl"))) {
		goto err_ctrl_cdev;
    	}

	cdev_init(&chat_cdev, &chat_fops);
	if (cdev_add(&chat_cdev, MKDEV(major_num, 1), NUM_CHATS) < 0) {
		goto err_ctrl_device;
	}

	// /dev/telegram/
        // Минор 1-NUM_CHATS — для сервера 
	for (int i = 1; i <= NUM_CHATS; ++i) {

		if (IS_ERR(device_create(tg_class, NULL, MKDEV(major_num, i), NULL, "telegram/chat_%d", i))) {

			while (i > 1) {
				--i;
             			device_destroy(tg_class, MKDEV(major_num, i));
            		}

			goto err_chat_cdev;
		}
	}

	printk(KERN_INFO "[TG_FS] Модуль загружен, major: %d\n", major_num);

	return 0;

err_chat_cdev:
	cdev_del(&chat_cdev);
err_ctrl_device:
	device_destroy(tg_class, MKDEV(major_num, 0));
err_ctrl_cdev:
	cdev_del(&ctrl_cdev);
err_class:
	class_destroy(tg_class);
	unregister_chrdev_region(MKDEV(major_num, 0), 1 + NUM_CHATS);
err_message:
        printk(KERN_ALERT "[TG_FS] Ошибка инициализации, откат выполнен.\n");
        return -1;
}

static void __exit tg_exit(void) {
	device_destroy(tg_class, MKDEV(major_num, 0));

	for (int i = 1; i <= NUM_CHATS; ++i) {
		device_destroy(tg_class, MKDEV(major_num, i));
	}

	cdev_del(&ctrl_cdev);
	cdev_del(&chat_cdev);

	class_destroy(tg_class);
	unregister_chrdev_region(MKDEV(major_num, 0), 1 + NUM_CHATS);

	printk(KERN_INFO "[TG_FS] Модуль выгружен.\n");
}

module_init(tg_init);
module_exit(tg_exit);
