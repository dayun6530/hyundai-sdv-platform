import asyncio


async def main() -> None:
    try:
        import websockets  # type: ignore
    except Exception as e:  # pragma: no cover
        print("NO_WEBSOCKETS_LIB", repr(e))
        return

    try:
        async with websockets.connect(
            "ws://127.0.0.1:9001/mqtt",
            subprotocols=["mqtt"],
            open_timeout=3,
            close_timeout=1,
        ):
            print("WS_OK")
    except Exception as e:
        print("WS_FAIL", repr(e))


if __name__ == "__main__":
    asyncio.run(main())

