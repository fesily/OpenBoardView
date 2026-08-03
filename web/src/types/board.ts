/** boardSchemaVersion 1 — mirrors obv_core ExportBoardJson / design §5.2 */

export interface Point {
  x: number;
  y: number;
}

export interface Bounds {
  minX: number;
  minY: number;
  maxX: number;
  maxY: number;
}

export interface OutlineSegment {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
}

export interface Outline {
  points: Point[];
  segments: OutlineSegment[];
}

export interface Net {
  id: number;
  name: string;
  /** Desktop net->show_name; prefer for labels. */
  showName?: string;
  isGround: boolean;
}

export type BoardSide = 'top' | 'bottom' | 'both' | 'unknown' | string;
export type MountType = 'smd' | 'through_hole' | string;
export type ComponentType = string;
export type PinShape = 'circle' | 'rect' | 'square' | string;

export interface Component {
  name: string;
  side: BoardSide;
  mount: MountType;
  type: ComponentType;
  mfgcode: string;
  center: Point;
  outline: Point[];
  pins: string[];
}

export type PinType =
  | 'unknown'
  | 'not_connected'
  | 'component'
  | 'via'
  | 'test_pad'
  | string;

export interface Pin {
  id: string;
  component: string | null;
  number: string;
  name: string;
  /**
   * Display label (desktop pin->show_name). Prefer over name for canvas text.
   * Overlay partInfos[part].pins[key].show_name overrides when present.
   */
  show_name?: string;
  /**
   * Desktop Pin::EPinType. Preferred for NC / test-pad coloring over net name alone.
   * Missing PIN_NET in BVR used to look like NC via default net string — type is authoritative.
   */
  type?: PinType;
  netId: number | null;
  side: BoardSide;
  pos: Point;
  shape: PinShape;
  diameter: number;
  size: Point;
  angle: number;
  /**
   * Board-file measurement fields (BVR PIN_DIODE_VALUE / PIN_VOLTAGE_VALUE, etc).
   * Overlay partInfos may override the same keys for display.
   */
  diode?: string;
  voltage?: string;
  ohm?: string;
  ohm_black?: string;
}

export interface Track {
  side: BoardSide;
  start: Point;
  end: Point;
  width: number;
  netId: number | null;
}

export interface Via {
  side: BoardSide;
  targetSide: BoardSide;
  pos: Point;
  size: number;
  netId: number | null;
}

export interface Arc {
  side: BoardSide;
  pos: Point;
  radius: number;
  width: number;
  startAngle: number;
  endAngle: number;
  netId: number | null;
}

export interface BoardDocument {
  boardSchemaVersion: 1;
  boardId: string;
  sourceName: string;
  bounds: Bounds;
  sides: string[];
  outline: Outline;
  nets: Net[];
  components: Component[];
  pins: Pin[];
  tracks: Track[];
  vias: Via[];
  arcs: Arc[];
}

export type BoardMeta = Pick<
  BoardDocument,
  'boardSchemaVersion' | 'boardId' | 'sourceName' | 'bounds' | 'sides'
>;

/** GET /api/v1/boards list entry */
export interface BoardSummary {
  id: string;
  name: string;
  ok: boolean;
  error: string;
  /** Relative path from boardRoot when provided by library scan */
  path?: string;
}


// --- Overlays (desktop-compatible PartInfos / NetInfos + freeform annotations)

export type PinVoltageFlag = 'unknown' | 'input' | 'output' | string;

export interface PinInfo {
  show_name?: string;
  diode?: string;
  voltage?: string;
  ohm?: string;
  ohm_black?: string;
  note?: string;
  voltage_flag?: PinVoltageFlag;
}

export interface PartInfo {
  part_type?: string;
  /** degrees when present: 0 | 90 | 180 | 270 — desktop PartAngle enum ints also accepted */
  angle?: number;
  pins?: Record<string, PinInfo>;
  /** Board-local operating condition groups (merged with chip library on read). */
  operating_conditions?: OperatingCondition[];
}

export interface OperatingCondition {
  id?: string;
  name?: string;
  inputs?: string[];
  outputs?: string[];
  enables?: string[];
  note?: string;
}

export type ConditionSource = 'board' | 'chip' | 'none';

/** Chip-library pin map entry (datasheet id ↔ name + aliases). */
export interface ChipPin {
  id: string;
  name?: string;
  aliases?: string[];
  dir?: string;
  note?: string;
}

/** How a condition label matched a chip pin. */
export type ChipPinMatched = 'none' | 'id' | 'name' | 'alias' | string;

/** Per-label resolve annotation on condition pin lists. */
export interface ResolvedPinLabel {
  label: string;
  matched: ChipPinMatched;
  id?: string;
  name?: string;
}

/** Resolved inputs/outputs/enables for one condition group. */
export interface ConditionResolvedPins {
  inputs?: ResolvedPinLabel[];
  outputs?: ResolvedPinLabel[];
  enables?: ResolvedPinLabel[];
}

/** GET .../parts/:part/operating-conditions merged view. */
export interface PartConditionsView {
  boardId?: string;
  part?: string;
  part_type?: string;
  source: ConditionSource;
  /** Effective list after merge (board wins when non-empty). */
  effective: OperatingCondition[];
  /** Alias some responses use instead of effective. */
  operating_conditions?: OperatingCondition[];
  board: OperatingCondition[];
  chip: OperatingCondition[];
  /** Chip pin table when part_type is bound; empty when unbound/missing. */
  chipPins?: ChipPin[];
  /** Per effective condition id → resolved label annotations. */
  resolved?: Record<string, ConditionResolvedPins>;
}

export interface NetInfo {
  showname?: string;
  note?: string;
}

export interface OverlayAnnotation {
  id: number;
  side: number;
  x: number;
  y: number;
  net: string;
  part: string;
  pin: string;
  note: string;
  visible: boolean;
}

export interface OverlayDocument {
  annotations: OverlayAnnotation[];
  partInfos: Record<string, PartInfo>;
  netInfos: Record<string, NetInfo>;
}

export interface NewAnnotation {
  side?: number;
  x: number;
  y: number;
  net?: string;
  part?: string;
  pin?: string;
  note?: string;
}

export interface ApiErrorBody {
  error: {
    code: string;
    message: string;
  };
}

export interface HealthResponse {
  status: string;
}

export interface VersionResponse {
  server: string;
  serverVersion: string;
  core?: string;
}
