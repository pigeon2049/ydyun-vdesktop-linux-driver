#include "../../kernel/mt_fw_connection.h"

int connect_fw(const struct mt_fw_connection_ops *ops, void *opaque)
{
	return mt_fw_connect(ops, opaque);
}

int disconnect_fw(const struct mt_fw_connection_ops *ops, void *opaque)
{
	return mt_fw_disconnect(ops, opaque);
}
