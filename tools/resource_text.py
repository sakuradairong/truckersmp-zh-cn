"""Localize identified text fields while preserving the surrounding resource."""
from html import escape
from html.parser import HTMLParser
import re

from common import require


def pattern(text):
    return r'\s+'.join(re.escape(word) for word in text.split())


def same_text(left, right):
    return isinstance(left, str) and isinstance(right, str) and left.split() == right.split()


def splice(text, edits):
    cursor, parts = 0, []
    for start, stop, replacement in sorted(edits, key=lambda item: (item[0], item[1])):
        require(cursor <= start <= stop <= len(text), 'Overlapping resource edits')
        parts.extend((text[cursor:start], replacement))
        cursor = stop
    return ''.join((*parts, text[cursor:]))


def css_rules(text):
    # Comments are masked so rule positions and selectors remain meaningful.
    masked = re.sub(r'/\*.*?\*/', lambda match: ' ' * len(match[0]), text, flags=re.S)
    rules = []
    for match in re.finditer(r'([^{}]+)\{([^{}]*)\}', masked):
        selector = ' '.join(match[1].split())
        properties = {}
        for declaration in match[2].split(';'):
            if ':' in declaration:
                name, value = declaration.split(':', 1)
                properties[name.strip()] = ' '.join(value.split())
        rules.append((selector, properties))
    return rules


def css_overrides(text, rules):
    existing = {selector for selector, _ in css_rules(text)}
    result = []
    for rule in rules:
        selector = rule['selector']
        require(not rule.get('required', True) or selector in existing,
                'Missing style selector: ' + selector)
        require(rule['properties'] and not any(char in selector for char in '{}'), 'Invalid style rule')
        declarations = []
        for name, value in rule['properties'].items():
            require(not any(char in name + value for char in '{};'), 'Invalid style property')
            declarations.append(f'    {name}: {value};')
        result.append(selector + ' {\n' + '\n'.join(declarations) + '\n}')
    return '\n\n/* Chinese localization */\n' + '\n\n'.join(result) + '\n' if result else ''


class Document(HTMLParser):
    """Keep original source spans; never serialize bindings through an HTML writer."""
    VOID = {'img', 'input', 'br', 'hr', 'link', 'meta'}

    def __init__(self, text):
        super().__init__(convert_charrefs=False)
        self.text, self.nodes, self.stack = text, [], []
        self.lines = [0]
        self.lines.extend(match.end() for match in re.finditer('\n', text))
        self.feed(text)
        self.close()
        require(not self.stack, 'Unclosed RML element')

    def source_offset(self):
        line, column = self.getpos()
        return self.lines[line - 1] + column

    def start(self, tag, attrs, closed):
        require(len(dict(attrs)) == len(attrs), 'Duplicate RML attribute')
        node = dict(tag=tag, attrs=dict(attrs), start=self.source_offset(),
                    end=self.source_offset() + len(self.get_starttag_text()),
                    parent=self.stack[-1] if self.stack else None, data=[], children=[], close=None)
        if node['parent'] is not None:
            node['parent']['children'].append(node)
        self.nodes.append(node)
        if not closed and tag not in self.VOID:
            self.stack.append(node)

    def handle_starttag(self, tag, attrs):
        self.start(tag, attrs, False)

    def handle_startendtag(self, tag, attrs):
        self.start(tag, attrs, True)

    def handle_endtag(self, tag):
        require(self.stack and self.stack[-1]['tag'] == tag, 'Unbalanced RML element: ' + tag)
        self.stack.pop()['close'] = self.source_offset()

    def handle_data(self, data):
        if self.stack:
            self.stack[-1]['data'].append((self.source_offset(), self.source_offset() + len(data)))


def matches(node, selector):
    if selector.get('tag', node['tag']) != node['tag']:
        return False
    if selector.get('no_attrs') and node['attrs']:
        return False
    if any(node['attrs'].get(key) != value for key, value in selector.get('attrs', {}).items()):
        return False
    if 'within' in selector:
        parent = node['parent']
        while parent is not None and not matches(parent, selector['within']):
            parent = parent['parent']
        return parent is not None
    return True


def rml(text, entry):
    document = Document(text)
    edits = []
    for edit in entry['edits']:
        found = []
        mode = edit.get('mode', 'text')
        for node in document.nodes:
            if not matches(node, edit['selector']):
                continue
            if mode in ('append', 'after'):
                position = node['close'] if mode == 'append' else node['end']
                require(position is not None, 'Missing closing target element')
                found.append((position, position, edit['text']))
            elif mode == 'attribute':
                attribute = edit['attribute']
                if not same_text(node['attrs'].get(attribute, ''), edit['source']):
                    continue
                opening = text[node['start']:node['end']]
                values = list(re.finditer(r'(?<![\w:-])' + re.escape(attribute) +
                                         r'\s*=\s*([\'"])(.*?)\1', opening, re.S))
                require(len(values) == 1, 'Ambiguous RML attribute: ' + attribute)
                value = values[0]
                found.append((node['start'] + value.start(2), node['start'] + value.end(2),
                              escape(edit['text'], quote=True)))
            else:
                require(mode in ('text', 'literal'), 'Unknown text edit mode')
                if edit['source'] == '' and not node['children'] and not any(
                        text[a:b].strip() for a, b in node['data']):
                    require(node['close'] is not None, 'Missing empty text target')
                    found.append((node['end'], node['close'], edit['text']))
                for start, stop in node['data']:
                    value = text[start:stop]
                    if mode == 'text' and not same_text(value, edit['source']):
                        continue
                    if not edit['source']:
                        continue
                    for match in re.finditer(pattern(edit['source']), value):
                        found.append((start + match.start(), start + match.end(), edit['text']))
        require(len(found) == edit.get('count', 1),
                'Missing or ambiguous text target: ' + edit.get('source', str(edit['selector'])))
        edits.extend(found)
    if entry.get('styles'):
        styles = [node for node in document.nodes if node['tag'] == 'style']
        require(len(styles) == 1 and styles[0]['close'] is not None, 'Missing or ambiguous inline stylesheet')
        node = styles[0]
        edits.append((node['close'], node['close'],
                      css_overrides(text[node['end']:node['close']], entry['styles'])))
    return splice(text, edits)


def apply(raw, entry):
    text = raw.decode('utf-8')
    crlf = '\r\n' in text and '\n' not in text.replace('\r\n', '')
    text = text.replace('\r\n', '\n')
    name = entry['name']
    try:
        if name.endswith('.rml'):
            result = rml(text, entry)
        elif name.endswith('.rcss'):
            result = text + css_overrides(text, entry['styles'])
        else:
            require(name.endswith('.lua') and len(entry['edits']) == 1, 'Unknown resource text format')
            edit = entry['edits'][0]
            expression = (r'GetElementById\(\s*([\'"])' + re.escape(edit['element_id']) +
                          r'\1\s*\)\s*\.inner_rml\s*=\s*([\'"])(.*?)\2')
            found = list(re.finditer(expression, text))
            require(len(found) == 1 and same_text(found[0][3], edit['source']),
                    'Missing or ambiguous Lua title assignment')
            match = found[0]
            result = splice(text, [(match.start(3), match.end(3), edit['text'])])
    except ValueError as exc:
        raise ValueError('Incompatible resource ' + name + ': ' + str(exc)) from exc
    return result.replace('\n', '\r\n').encode('utf-8') if crlf else result.encode('utf-8')
