"""Build-time raw JSON checks; runtime schema validation is performed by Qt."""
import json
import pathlib
import sys


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def decode(data):
    if data.startswith(b"\xef\xbb\xbf"):
        raise ValueError("UTF-8 BOM is forbidden")
    return json.loads(data.decode("utf-8"), object_pairs_hook=unique,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        for bad in [b'{"a":1,"a":2}', b'{"x":{"a":1,"a":2}}', b'{"a":NaN}', b'\xef\xbb\xbf{}', b'\xff', b'{"a":1,}']:
            try:
                decode(bad)
            except (ValueError, UnicodeError):
                continue
            raise AssertionError(f"accepted invalid data: {bad!r}")
        assert decode(b'{"a":{"b":1},"c":{"b":2}}') == {"a": {"b": 1}, "c": {"b": 2}}
    else:
        for name in sys.argv[1:]:
            try:
                decode(pathlib.Path(name).read_bytes())
            except (ValueError, UnicodeError) as error:
                sys.exit(f"{name}: {error}")
