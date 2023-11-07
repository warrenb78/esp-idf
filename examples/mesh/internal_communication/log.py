import logging

FORMAT = '%(asctime)s [%(levelname)s] %(message)s'

def config_logging(output_path, stdout_level=logging.INFO):
    file_handler = logging.FileHandler(output_path)
    stream_handler = logging.StreamHandler()
    stream_handler.setLevel(stdout_level)
    logging.basicConfig(
        format=FORMAT,
        level=logging.DEBUG,
        handlers=[
            file_handler,
            stream_handler
        ]
    )
