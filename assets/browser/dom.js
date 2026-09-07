/* REIST browser DOM subset, profile 1. Executed only in isolated JSWORK.
 * HTMLWORK owns the real tree; this synchronous view produces an admitted
 * mutation journal, never HTML strings or OS requests. */
(() => {
    const wrappers = new Map(), brands = new WeakMap();
    let nodes = [], url = '', pending = '', mutations = 0, newTitle, failed = false;
    const quota = () => { failed = true; throw new RangeError('REIST DOM quota'); };
    const text = (value, nullable = false) => {
        if (typeof value === 'symbol') throw new TypeError('DOMString Symbol');
        return nullable && value === null ? '' : String(value);
    };
    const record = (id, value) => {
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
        const item = id.toString(16).padStart(8, '0') + (result.length / 2).toString(16).padStart(8, '0') + result;
        if (mutations === 128 || pending.length + item.length >= 65536)
            quota();
        pending += item; ++mutations;
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
        set id(value) { throw new TypeError('id mutation is not in REIST DOM profile 1'); }
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
        sync(address, data) {
            if (pending || !Array.isArray(data) || data.length > 8192) throw new TypeError('DOM snapshot');
            nodes = data; url = address; newTitle = undefined;
        },
        take() { if (failed) throw new RangeError('Failed DOM journal'); const result = pending; pending = ''; mutations = 0; return result; }
    });
    Object.defineProperties(globalThis, {
        window: {value: globalThis}, self: {value: globalThis},
        document: {value: document}, __reistDOM: {value: bridge}
    });
})();
