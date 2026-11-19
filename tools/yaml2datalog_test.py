#!/usr/bin/env python3
"""Self-check for yaml2datalog.translate. Run: python3 tools/yaml2datalog_test.py"""
import yaml2datalog as y


def rule(equalities, thresholds):
    return {
        "Preconditions": {
            "Relations": [[0, 0, 1, 2, 3, 4], [1, 0, 1, 2, 3, 4]],
            "Equalities": [equalities],
            "Sim": [[0, 1, 1, 1, 2, 2]],
            "Threshold": [[0, 1] + thresholds],
        },
        "Conseq": {"Equality": [0, 1, 0, 0]},
    }


def main():
    # year equality blocking -> shared variable k0 on column 4.
    text = y.translate(rule([0, 1, 4, 4], [0.81, 0.79]), [5, 5],
                       ["tableA", "tableB"], "match", "jaro_winkler")
    assert "match(a0, b0) :-" in text, text
    assert "tableA(a0, a1, a2, a3, k0)" in text, text
    assert "tableB(b0, b1, b2, b3, k0)" in text, text
    assert "jaro_winkler(a1, b1, 0.81)" in text, text
    assert "jaro_winkler(a2, b2, 0.79)" in text, text

    # no equality (DBLP-Google form) -> no shared variable.
    text2 = y.translate(rule([0, 1], [0.88, 0.85]), [5, 5],
                        ["tableA", "tableB"], "match", "jaro_winkler")
    assert "k0" not in text2, text2
    assert "jaro_winkler(a1, b1, 0.88)" in text2, text2
    assert "jaro_winkler(a2, b2, 0.85)" in text2, text2
    print("yaml2datalog self-check ok")


if __name__ == "__main__":
    main()