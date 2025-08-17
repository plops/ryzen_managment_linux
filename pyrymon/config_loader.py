import tomli

def load_config(path="config.toml"):
    """Loads the metrics configuration from a TOML file."""
    try:
        with open(path, "rb") as f:
            return tomli.load(f).get("metrics", {})
    except FileNotFoundError:
        print(f"Error: Configuration file not found at '{path}'")
        return {}
    except tomli.TOMLDecodeError as e:
        print(f"Error parsing TOML file: {e}")
        return {}