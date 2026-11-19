"""Start SmartWalk from the repository root."""

from api.server import LOCATION, create_server, main

__all__ = ["LOCATION", "create_server", "main"]

if __name__ == "__main__":
    main()
