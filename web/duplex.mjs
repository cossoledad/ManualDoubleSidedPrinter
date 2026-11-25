export const blank = 0;

export function planPages(expression, count) {
  const text = expression.trim();
  const selected = new Set();
  const add = (first, last) => {
    if (first < 1 || last > count || last < first) {
      throw new RangeError(`页码须在 1 到 ${count} 之间，且范围起点不能大于终点。`);
    }
    for (let page = first; page <= last; page += 1) selected.add(page);
  };

  if (!text || /^all$/i.test(text)) {
    add(1, count);
  } else {
    for (const token of text.split(/[,，]/).map((item) => item.trim()).filter(Boolean)) {
      const match = /^(\+?\d+)(?:\s*-\s*(\+?\d+))?$/.exec(token);
      if (!match) throw new RangeError('请输入有效页码，例如 1-3,5,8-10。');
      add(Number(match[1]), Number(match[2] ?? match[1]));
    }
  }
  if (!selected.size) throw new RangeError('至少选择一页。');

  const first = [];
  const second = [];
  const pages = [...selected].sort((a, b) => a - b);
  for (const [index, page] of pages.entries()) (index % 2 ? second : first).push(page);
  second.reverse();
  if (pages.length > 1 && pages.length % 2) second.unshift(blank);
  return { first, second };
}
