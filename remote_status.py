# This Python program is an example of the use of remote_picotool
# as a Python module providing remote procedure call (RPC) functionality.
#
# remote_picotool is used to call the "remote_handler_get_status" function
# within fw/main.c, obtaining a text report of the current system status.
#
# The update_secret and board_id needed to access Pico 2 W via the network
# are loaded from remote_picotool.cfg.

import asyncio
import remote_picotool

ID_GET_STATUS_HANDLER = remote_picotool.ID_FIRST_USER_HANDLER + 0

async def remote_handler_get_status() -> bytes:
    try:
        config = remote_picotool.RemotePicotoolCfg()
        reader, writer = await remote_picotool.get_pico_connection(config)
        client = remote_picotool.Client(config.update_secret_hash, reader, writer)
        (result_data, result_value) = await client.run(ID_GET_STATUS_HANDLER)
        return result_data
    finally:
        writer.close()
        await writer.wait_closed()

async def run() -> None:
    result_data = await remote_handler_get_status()
    result_text = result_data.decode("utf-8", errors="ignore")
    print(result_text)

if __name__ == "__main__":
    asyncio.run(run())
