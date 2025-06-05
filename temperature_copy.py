# This Python program is an example of the use of remote_picotool
# as a Python module providing remote procedure call (RPC) functionality.
#
# Every 120 seconds, remote_picotool is used to call 
# the "remote_handler_get_status" function within fw/main.c
# to download up to 2000 samples from the thermistor ADC (which
# is sampling the temperature 10 times each second).
# This data was used to produce img/graph.png.
#
# The update_secret and board_id needed to access Pico 2 W via the network
# are loaded from remote_picotool.cfg.


import asyncio
import remote_picotool
import time
import struct

ID_GET_STATUS_HANDLER = remote_picotool.ID_FIRST_USER_HANDLER + 0

async def run() -> None:
    capture_period = 0.1
    download_period = 120.0
    previous_end_time = 0.0
    config = remote_picotool.RemotePicotoolCfg()
    reader, writer = await remote_picotool.get_pico_connection(config)
    try:
        client = remote_picotool.Client(config.update_secret_hash, reader, writer)
        with open("temp_log.txt", "at", encoding="utf-8") as fd:
            while True:
                request_time = time.time() 
                (result_data, result_value) = await client.run(ID_GET_STATUS_HANDLER, parameter = 2)
                if result_value != 0:
                    raise Exception(f"result value {result_value}")
                if len(result_data) & 1:
                    raise Exception("result data size is odd")
                report_size = len(result_data) // 2
                report_start_time = max(request_time - (report_size * capture_period), previous_end_time)
                print(f"Got {report_size} items starting at {report_start_time:1.2f}", flush=True)
                for (i, item) in enumerate(struct.unpack(f"<{report_size}H", result_data)):
                    t = report_start_time + (i * capture_period)
                    fd.write(f"{t:1.2f} {item:d}\n")

                previous_end_time = report_start_time + (report_size * capture_period)
                fd.flush()
                await asyncio.sleep(download_period)

    except KeyboardInterrupt:
        pass
    finally:
        writer.close()
        await writer.wait_closed()

if __name__ == "__main__":
    asyncio.run(run())
