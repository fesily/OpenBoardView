import type { Net, OverlayDocument } from '../types/board';

/** overlay showname > board showName > board name */
export function netDisplayName(
  net: Pick<Net, 'name' | 'showName'> | null | undefined,
  overlay?: OverlayDocument | null,
): string {
  if (!net) return '';
  const ov = overlay?.netInfos?.[net.name]?.showname?.trim() || '';
  if (ov) return ov;
  const boardShow = (net.showName ?? '').trim();
  if (boardShow) return boardShow;
  return net.name;
}
