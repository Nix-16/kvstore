#ifndef KVS_SNAPSHOT_H
#define KVS_SNAPSHOT_H

/*
 * kvs_snapshot_save
 * 将当前内存数据库做一次全量快照，写入 snapshot_file
 *
 * @return:
 *  0   success
 * -1   snapshot 未启用/配置错误
 * -2   打开临时文件失败
 * -3   写文件失败
 * -4   fsync/close/rename 失败
 * -5   遍历数据库失败
 */
int kvs_snapshot_save(void);

/*
 * kvs_snapshot_load
 * 从 snapshot_file 加载全量快照到当前内存数据库
 *
 * @return:
 *  0   success
 *  1   snapshot 文件不存在（可视为正常，无需恢复）
 * -1   snapshot 未启用/配置错误
 * -2   打开文件失败
 * -3   文件格式非法
 * -4   读文件失败
 * -5   写回内存数据库失败
 */
int kvs_snapshot_load(void);

#endif