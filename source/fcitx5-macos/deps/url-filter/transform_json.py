#!/usr/bin/env python3

import json


def cpp_vector(l: list[str]) -> str:
    return '{' + ', '.join(json.dumps(s) for s in l) + '}'


with open('Rules/data.min.json', 'r') as file:
    data = json.load(file)

results = []

for o in data['providers'].values():
    rules = o.get('rules', [])
    rawRules = o.get('rawRules', [])
    redirections = o.get('redirections', [])
    if not rules and not rawRules and not redirections:
        continue
    results.append({
        'urlPattern': o['urlPattern'],
        'rules': rules,
        'rawRules': rawRules,
        'exceptions': o.get('exceptions', []),
        'redirections': redirections
    })

with open('src/rules.cpp', 'w') as file:
    file.write('''
#include "rules.hpp"

namespace url_filter {
std::vector<Rule> rules = {''')

    for rule in results:
        file.write('''
    Rule({}, {}, {}, {}, {}),'''
                   .format(
                       json.dumps(rule['urlPattern']),
                       cpp_vector(rule['rules']),
                       cpp_vector(rule['rawRules']),
                       cpp_vector(rule['exceptions']),
                       cpp_vector(rule['redirections'])
                    ))

    file.write('''
};
}
''')
