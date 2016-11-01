export const PROJECT_CREATE = 'PROJECT_CREATE';
export const PROJECT_RENAME = 'PROJECT_RENAME';
export const PROJECT_LOAD_DATA = 'PROJECT_LOAD_DATA';

export const NODE_MOVE = 'NODE_MOVE';
export const NODE_ADD = 'NODE_ADD';
export const NODE_DELETE = 'NODE_DELETE';
export const NODE_UPDATE_PROPERTY = 'NODE_UPDATE_PROPERTY';
export const NODE_CHANGE_PIN_MODE = 'NODE_CHANGE_PIN_MODE';

export const LINK_ADD = 'LINK_ADD';
export const LINK_DELETE = 'LINK_DELETE';

export const META_UPDATE = 'META_UPDATE';

export const FOLDER_ADD = 'FOLDER_ADD';
export const FOLDER_RENAME = 'FOLDER_RENAME';
export const FOLDER_DELETE = 'FOLDER_DELETE';
export const FOLDER_MOVE = 'FOLDER_MOVE';

export const PATCH_ADD = 'PATCH_ADD';
export const PATCH_RENAME = 'PATCH_RENAME';
export const PATCH_DELETE = 'PATCH_DELETE';
export const PATCH_MOVE = 'PATCH_MOVE';

export const getPatchUndoType = (id) => `@@redux-undo/PATCH_${id}_UNDO`;
export const getPatchRedoType = (id) => `@@redux-undo/PATCH_${id}_REDO`;
export const getPatchClearHistoryType = (id) => `@@redux-undo/PATCH_${id}_CLEAR_HISTORY`;