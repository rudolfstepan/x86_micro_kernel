/* REIST browser DOM subset, profiles 1/2. Executed only in isolated JSWORK.
 * HTMLWORK owns the real tree; this synchronous view produces an admitted
 * mutation journal, never HTML strings or OS requests. */
(() => {
    const wrappers = new Map(), brands = new WeakMap(), lists = new Map(), listBrands = new WeakMap();
    let nodes = [], url = '', pending = '', mutations = 0, profile = 1, newTitle, failed = false;
    const quota = () => { failed = true; throw new RangeError('REIST DOM quota'); };
    const domError = name => { const e = new Error(name); e.name = name; throw e; };
    const text = (value, nullable = false) => {
        if (typeof value === 'string') return value;
        if (typeof value === 'symbol') throw new TypeError('DOMString Symbol');
        return nullable && value === null ? '' : String(value);
    };
    const record = (id, value, operation = 0, name = '') => {
        let n, a, index = -1;
        if (operation) {
            n = nodes[id - 1];
            if (!n || n[0] !== 1) throw new TypeError('Element receiver');
            if (profile !== 2 || n[1] !== 1) domError('NotSupportedError');
            a = n[9];
            for (let i = 0; i < a.length; ++i) if (a[i][0] === name) { index = i; break; }
            if (operation === 2 && index < 0) return;
        }
        let result = '';
        for (const c of value) {
            let n = c.codePointAt(0), bytes;
            if (n >= 0xd800 && n <= 0xdfff) n = 0xfffd;
            if (n < 0x80) bytes = [n];
            else if (n < 0x800) bytes = [0xc0 | n >> 6, 0x80 | n & 63];
            else if (n < 0x10000) bytes = [0xe0 | n >> 12, 0x80 | n >> 6 & 63, 0x80 | n & 63];
            else bytes = [0xf0 | n >> 18, 0x80 | n >> 12 & 63, 0x80 | n >> 6 & 63, 0x80 | n & 63];
            for (const b of bytes) result += b.toString(16).padStart(2, '0');
            if (result.length > 65000) quota();
        }
        let item;
        if (profile === 1) item = id.toString(16).padStart(8, '0') + (result.length / 2).toString(16).padStart(8, '0') + result;
        else {
            let encoded = '';
            for (let i = 0; i < name.length; ++i) encoded += name.charCodeAt(i).toString(16).padStart(2, '0');
            item = operation.toString(16).padStart(8, '0') + id.toString(16).padStart(8, '0') +
                name.length.toString(16).padStart(8, '0') + (result.length / 2).toString(16).padStart(8, '0') + encoded + result;
        }
        if (mutations === 128 || pending.length + item.length >= 65536)
            quota();
        pending += item; ++mutations;
        if (operation) {
            if (operation === 2) { for (let i = index; i + 1 < a.length; ++i) a[i] = a[i + 1]; --a.length; }
            else if (index < 0) a.push([name, value]);
            else a[index][1] = value;
            if (name === 'id') n[6] = operation === 2 ? '' : value;
        }
    };
    const visit = (id, mode, key) => {
        // The interpreter's accepted 16-KiB native stack is not a DOM stack.
        // Iterative preorder keeps deep documents in the private JS heap.
        const todo = [[id, 0]]; let work = 0, value = '';
        while (todo.length) {
            const [current, depth] = todo.pop(), n = nodes[current - 1];
            if (!n) continue;
            if (depth >= 128 || ++work > 1048576) quota();
            if (mode === 1 && n[0] === 1 && n[1] === 1 && n[5] === key) return current;
            if (mode === 2 && n[0] === 1 && n[6] === key) return current;
            if (mode === 3) {
                if (n[8] !== undefined) value += n[8];
                else if (n[0] === 3) value += n[7];
            }
            const children = []; let child = n[3];
            while (child) {
                if (children.length >= nodes.length || ++work > 1048576) quota();
                children.push(child); child = nodes[child - 1][4];
            }
            for (let i = children.length - 1; i >= 0; --i) todo.push([children[i], depth + 1]);
        }
        return mode === 3 ? value : 0;
    };
    const replace = (id, value) => {
        const n = nodes[id - 1]; n[3] = 0; n[8] = value;
    };
    const html = id => {
        const n = nodes[id - 1];
        if (!n || n[0] !== 1) throw new TypeError('Element receiver');
        if (profile !== 2 || n[1] !== 1) domError('NotSupportedError');
        return n;
    };
    const lower = value => {
        let result = '';
        for (let i = 0; i < value.length; ++i) {
            const c = value.charCodeAt(i); result += c >= 65 && c <= 90 ? String.fromCharCode(c + 32) : value[i];
        }
        return result;
    };
    const attributeName = name => {
        if (!name) domError('InvalidCharacterError');
        if (name.length > 255) quota();
        for (let i = 0; i < name.length; ++i) {
            const c = name.charCodeAt(i);
            if (c > 127) domError('NotSupportedError');
            if (!(c >= 97 && c <= 122 || c === 95 || c === 58 || i && (c >= 48 && c <= 57 || c === 45 || c === 46)))
                domError('InvalidCharacterError');
        }
        return name;
    };
    const attributeValue = (id, name) => {
        const n = nodes[id - 1];
        if (!n || n[0] !== 1) throw new TypeError('Element receiver');
        if (profile !== 2 || n[1] !== 1) domError('NotSupportedError');
        const a = n[9];
        for (let i = 0; i < a.length; ++i) if (a[i][0] === name) return a[i][1];
        return null;
    };
    const tokenSet = id => {
        const n = nodes[id - 1];
        if (!n || n[0] !== 1) throw new TypeError('Element receiver');
        if (profile !== 2 || n[1] !== 1) domError('NotSupportedError');
        let value = ''; const a = n[9], result = [];
        for (let i = 0; i < a.length; ++i) if (a[i][0] === 'class') { value = a[i][1]; break; }
        let token = '';
        for (let i = 0; i <= value.length; ++i) {
            const c = value.charCodeAt(i);
            if (i === value.length || c === 9 || c === 10 || c === 12 || c === 13 || c === 32) {
                if (token && !result.includes(token)) { if (result.length === 1024) quota(); result.push(token); }
                token = '';
            } else token += value[i];
        }
        return result;
    };
    const tokenValue = value => {
        if (!value) domError('SyntaxError');
        for (let i = 0; i < value.length; ++i) {
            const c = value.charCodeAt(i);
            if (c === 9 || c === 10 || c === 12 || c === 13 || c === 32) domError('InvalidCharacterError');
        }
        return value;
    };
    const tokenIterator = (id, kind) => {
        let index = 0, done = false;
        return {
            next() {
                if (done) return {done: true};
                // Array.from invokes next under a native frame. Keep parsing
                // flat here rather than nesting tokenSet below that frame.
                const a = nodes[id - 1][9], tokens = []; let value = '', token = '';
                for (let i = 0; i < a.length; ++i) if (a[i][0] === 'class') { value = a[i][1]; break; }
                for (let i = 0; i <= value.length; ++i) {
                    const c = value.charCodeAt(i);
                    if (i === value.length || c === 9 || c === 10 || c === 12 || c === 13 || c === 32) {
                        if (token && !tokens.includes(token)) { if (tokens.length === 1024) quota(); tokens.push(token); }
                        token = '';
                    } else token += value[i];
                }
                if (index >= tokens.length) { done = true; return {done: true}; }
                const at = index++;
                return {done: false, value: kind === 1 ? at : kind === 2 ? [at, tokens[at]] : tokens[at]};
            },
            [Symbol.iterator]() { return this; }
        };
    };
    // Iterative helpers keep native interpreter call depth bounded. Tokens and
    // variadic argument conversions are all admitted before publishing a write.
    class ClassList {
        get length() { return tokenSet(listBrands.get(this)).length; }
        item(index) { if (!arguments.length) throw new TypeError('index required'); index = +index; return tokenSet(listBrands.get(this))[index >>> 0] ?? null; }
        contains(value) { if (!arguments.length) throw new TypeError('token required'); value = text(value); return tokenSet(listBrands.get(this)).includes(value); }
        add(...values) {
            if (values.length > 1024) quota();
            for (let i = 0; i < values.length; ++i) values[i] = text(values[i]);
            for (let i = 0; i < values.length; ++i) tokenValue(values[i]);
            const id = listBrands.get(this), tokens = tokenSet(id);
            for (let i = 0; i < values.length; ++i) if (!tokens.includes(values[i])) {
                if (tokens.length === 1024) quota(); tokens.push(values[i]);
            }
            if (tokens.length || attributeValue(id, 'class') !== null) record(id, tokens.join(' '), 1, 'class');
        }
        remove(...values) {
            if (values.length > 1024) quota();
            for (let i = 0; i < values.length; ++i) values[i] = text(values[i]);
            for (let i = 0; i < values.length; ++i) tokenValue(values[i]);
            const id = listBrands.get(this), tokens = tokenSet(id);
            for (let i = 0; i < values.length; ++i) { const at = tokens.indexOf(values[i]); if (at >= 0) tokens.splice(at, 1); }
            if (tokens.length || attributeValue(id, 'class') !== null) record(id, tokens.join(' '), 1, 'class');
        }
        toggle(value, force) {
            if (!arguments.length) throw new TypeError('token required');
            value = text(value);
            const id = listBrands.get(this), token = tokenValue(value), tokens = tokenSet(id), at = tokens.indexOf(token);
            if (at >= 0) {
                if (force !== undefined && force) return true;
                tokens.splice(at, 1); record(id, tokens.join(' '), 1, 'class'); return false;
            }
            if (force !== undefined && !force) return false;
            if (tokens.length === 1024) quota(); tokens.push(token);
            record(id, tokens.join(' '), 1, 'class'); return true;
        }
        replace(value, next) {
            if (arguments.length < 2) throw new TypeError('two tokens required');
            const id = listBrands.get(this); value = text(value); next = text(next);
            tokenValue(value); tokenValue(next);
            const tokens = tokenSet(id), at = tokens.indexOf(value), other = tokens.indexOf(next);
            if (at < 0) return false;
            if (other < 0) tokens[at] = next;
            else if (other !== at) { if (other > at) tokens[at] = next; tokens.splice(Math.max(at, other), 1); }
            record(id, tokens.join(' '), 1, 'class'); return true;
        }
        supports(value) { text(value); throw new TypeError('class has no supported-token vocabulary'); }
        get value() { return attributeValue(listBrands.get(this), 'class') || ''; }
        set value(value) { value = text(value); record(listBrands.get(this), value, 1, 'class'); }
        toString() { return attributeValue(listBrands.get(this), 'class') || ''; }
        values() { const id = listBrands.get(this); html(id); return tokenIterator(id, 0); }
        keys() { const id = listBrands.get(this); html(id); return tokenIterator(id, 1); }
        entries() { const id = listBrands.get(this); html(id); return tokenIterator(id, 2); }
        forEach(callback, receiver) {
            if (typeof callback !== 'function') throw new TypeError('callback');
            const id = listBrands.get(this);
            for (let i = 0;; ++i) { const t = tokenSet(id); if (i >= t.length) return; callback.call(receiver, t[i], i, this); }
        }
    }
    ClassList.prototype[Symbol.iterator] = ClassList.prototype.values;
    class Element {
        constructor(key, id) {
            if (key !== brands) throw new TypeError('Illegal constructor');
            brands.set(this, id);
        }
        get textContent() {
            const id = brands.get(this); if (!id) throw new TypeError('Element receiver');
            return visit(id, 3);
        }
        set textContent(value) {
            const id = brands.get(this); if (!id) throw new TypeError('Element receiver');
            value = text(value, true); record(id, value); replace(id, value);
        }
        get id() { return nodes[brands.get(this) - 1][6]; }
        set id(value) { if (profile === 1) throw new TypeError('id mutation requires profile 2'); value = text(value); record(brands.get(this), value, 1, 'id'); }
        getAttribute(name) { if (!arguments.length) throw new TypeError('name required'); name = text(name); name = lower(name); return attributeValue(brands.get(this), name); }
        hasAttribute(name) { if (!arguments.length) throw new TypeError('name required'); name = text(name); name = lower(name); return attributeValue(brands.get(this), name) !== null; }
        hasAttributes() { return html(brands.get(this))[9].length !== 0; }
        getAttributeNames() { const a = html(brands.get(this))[9], result = []; for (let i = 0; i < a.length; ++i) result.push(a[i][0]); return result; }
        setAttribute(name, value) { if (arguments.length < 2) throw new TypeError('name and value required'); name = text(name); value = text(value); name = lower(name); attributeName(name); record(brands.get(this), value, 1, name); }
        removeAttribute(name) {
            if (!arguments.length) throw new TypeError('name required');
            name = text(name); name = lower(name); const id = brands.get(this);
            if (attributeValue(id, name) !== null) { attributeName(name); record(id, '', 2, name); }
        }
        toggleAttribute(name, force) {
            if (!arguments.length) throw new TypeError('name required');
            name = text(name); name = lower(name); attributeName(name); const id = brands.get(this), present = attributeValue(id, name) !== null;
            const wanted = force !== undefined ? !!force : !present;
            if (wanted !== present) record(id, '', wanted ? 1 : 2, name);
            return wanted;
        }
        get className() { return attributeValue(brands.get(this), 'class') || ''; }
        set className(value) { value = text(value); record(brands.get(this), value, 1, 'class'); }
        get classList() {
            const id = brands.get(this); html(id);
            if (!lists.has(id)) { const list = Object.create(ClassList.prototype); listBrands.set(list, id); lists.set(id, list); }
            return lists.get(id);
        }
        set classList(value) { value = text(value); record(brands.get(this), value, 1, 'class'); }
        get tagName() {
            const n = nodes[brands.get(this) - 1]; return n[1] === 1 ? n[5].toUpperCase() : n[5];
        }
    }
    const wrap = id => {
        if (!id) return null;
        if (!wrappers.has(id)) {
            const element = Object.create(Element.prototype);
            brands.set(element, id); wrappers.set(id, element);
        }
        return wrappers.get(id);
    };
    const document = {
        get URL() { return url; },
        get documentURI() { return url; },
        get readyState() { return 'loading'; },
        get body() { return wrap(visit(1, 1, 'body')); },
        get title() {
            const id = visit(1, 1, 'title');
            const value = id ? visit(id, 3) : newTitle === undefined ? '' : newTitle;
            let result = '', space = false;
            for (let i = 0; i < value.length; ++i) {
                const c = value.charCodeAt(i);
                if (c === 9 || c === 10 || c === 12 || c === 13 || c === 32) space = !!result;
                else { if (space) result += ' '; result += value[i]; space = false; }
            }
            return result;
        },
        set title(value) {
            value = text(value); record(0, value);
            const id = visit(1, 1, 'title'); if (id) replace(id, value); else newTitle = value;
        },
        getElementById(value) {
            value = text(value); if (!value) return null;
            return wrap(visit(1, 2, value));
        }
    };
    const bridge = Object.freeze({
        sync(address, data, version = 1) {
            if (pending || !Array.isArray(data) || data.length > 8192 || version !== 1 && version !== 2) throw new TypeError('DOM snapshot');
            nodes = data; url = address; profile = version; newTitle = undefined;
        },
        take() { if (failed) throw new RangeError('Failed DOM journal'); const result = pending; pending = ''; mutations = 0; return result; }
    });
    Object.defineProperties(globalThis, {
        window: {value: globalThis}, self: {value: globalThis},
        document: {value: document}, __reistDOM: {value: bridge}
    });
})();
