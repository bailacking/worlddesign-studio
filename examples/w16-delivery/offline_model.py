"""Deterministic W16 adapter fixtures with simulated token reports."""

import json
import sys


def main():
    request = json.load(sys.stdin)
    version = sys.argv[1]
    value, tokens = {"east": (-112, (16, 7)), "west": (-104, (18, 9))}[version]
    assert request["project"]["id"] == "signal-relay"
    assert "scene.field.set" in request["operations"]
    if request["max_total_tokens"] < sum(tokens):
        # Deliberate over-reported usage exercises rejection before any write.
        assert version == "west"
    print(
        json.dumps(
            {
                "usage": {"input_tokens": tokens[0], "output_tokens": tokens[1]},
                "proposal": {
                    "operation": "scene.field.set",
                    "input": {"entity_id": "runner", "field": "x", "value": value},
                },
            }
        )
    )


if __name__ == "__main__":
    main()
