import * as R from 'ramda';
import shortid from 'shortid';
import * as XP from 'xod-project';

import * as EAT from './actionTypes';
import * as PAT from '../project/actionTypes';
import * as DAT from '../debugger/actionTypes';
import { REMOVE_SELECTION } from '../projectBrowser/actionTypes';

import { DEFAULT_PANNING_OFFSET } from '../project/nodeLayout';
import { MAIN_PATCH_PATH } from '../project/constants';
import {
  DEBUGGER_TAB_ID,
  FOCUS_AREAS,
  SELECTION_ENTITY_TYPE,
  TAB_TYPES,
  PANEL_IDS,
} from './constants';
import {
  createSelectionEntity,
  getNewSelection,
  getTabByPatchPath,
} from './utils';
import { setCurrentPatchOffset, switchPatchUnsafe } from './actions';

import { getInitialPatchOffset } from '../project/utils';

// =============================================================================
//
// Utils
//
// =============================================================================

const getTabs = R.prop('tabs');
const getCurrentTabId = R.prop('currentTabId');

const getTabById = R.curry((tabId, state) =>
  R.compose(R.propOr(null, tabId), getTabs)(state)
);

const getCurrentTab = R.converge(getTabById, [getCurrentTabId, R.identity]);

const isTabOpened = R.curry((tabId, state) =>
  R.compose(R.complement(R.isNil), getTabById(tabId))(state)
);

// focuses on a current tab
export const currentTabLens = R.lens(
  getCurrentTab,
  R.curry((newCurrentTab, state) =>
    R.unless(
      R.pipe(getCurrentTabId, R.isNil),
      R.assocPath(['tabs', getCurrentTabId(state)], newCurrentTab)
    )(state)
  )
);

const getBreadcrumbs = R.compose(R.prop('breadcrumbs'), getCurrentTab);
const getBreadcrumbActiveIndex = R.compose(
  R.propOr(-1, 'activeIndex'),
  getBreadcrumbs
);
const getBreadcrumbChunks = R.compose(R.propOr([], 'chunks'), getBreadcrumbs);

const isPatchOpened = R.curry((patchPath, state) =>
  R.compose(R.complement(R.isNil), getTabByPatchPath(patchPath), getTabs)(state)
);

const setTabOffset = R.curry((offset, tabId, state) =>
  R.compose(
    R.when(
      () => tabId === DEBUGGER_TAB_ID,
      newState =>
        R.assocPath(
          [
            'tabs',
            tabId,
            'breadcrumbs',
            'chunks',
            getBreadcrumbActiveIndex(newState),
            'offset',
          ],
          offset,
          newState
        )
    ),
    R.assocPath(['tabs', tabId, 'offset'], offset)
  )(state)
);

const getTabIdbyPatchPath = R.curry((patchPath, state) =>
  R.compose(
    R.propOr(null, 'id'),
    R.find(R.propEq('patchPath', patchPath)),
    R.values,
    getTabs
  )(state)
);

const listTabsByPatchPath = R.curry((patchPath, state) =>
  R.compose(R.filter(R.propEq('patchPath', patchPath)), R.values, getTabs)(
    state
  )
);

const syncTabOffset = R.curry((offset, state) => {
  const currentTab = getTabById(state.currentTabId, state);
  const currentPatchPath = currentTab.patchPath;
  if (!currentTab) return state;

  const tabIdsToSync = R.compose(
    R.map(R.prop('id')),
    R.reject(R.propEq('id', state.currentTabId)),
    listTabsByPatchPath(currentPatchPath)
  )(state);
  const syncOffsets = R.map(setTabOffset(offset), tabIdsToSync);

  return R.reduce((acc, fn) => fn(acc), state, syncOffsets);
});

const setPropsToTab = R.curry((id, props, state) =>
  R.compose(
    R.assocPath(['tabs', id], R.__, state),
    R.merge(R.__, props),
    getTabById(id)
  )(state)
);

const addTabWithProps = R.curry((id, type, patchPath, state) => {
  const tabs = R.prop('tabs')(state);
  const lastIndex = R.reduce(
    (acc, tab) => R.pipe(R.prop('index'), R.max(acc))(tab),
    -1,
    R.values(tabs)
  );
  const newIndex = R.inc(lastIndex);

  return setPropsToTab(
    id,
    {
      id,
      patchPath,
      index: newIndex,
      type,
      offset: DEFAULT_PANNING_OFFSET,
      editedAttachment: null,
    },
    state
  );
});

const addPatchTab = R.curry((newId, patchPath, state) => {
  if (!patchPath) return state;
  return addTabWithProps(newId, TAB_TYPES.PATCH, patchPath, state);
});

const applyTabSort = (tab, payload) => {
  if (R.not(R.has(tab.id, payload))) {
    return tab;
  }

  return R.assoc('index', payload[tab.id].index, tab);
};

const resetCurrentPatchPath = (reducer, state, project) => {
  const stateWithClearedTabs = R.assoc('tabs', {}, state);

  return R.compose(
    R.ifElse(R.isNil, R.always(stateWithClearedTabs), firstLocalPatch => {
      const firstPatchPath = XP.getPatchPath(firstLocalPatch);
      const offset = getInitialPatchOffset(firstPatchPath, project);

      return [
        switchPatchUnsafe(firstPatchPath),
        setCurrentPatchOffset(offset),
      ].reduce(reducer, stateWithClearedTabs);
    }),
    R.head,
    R.sortBy(XP.getPatchPath),
    XP.listLocalPatches
  )(project);
};

const clearSelection = R.flip(R.merge)({
  selection: [],
  linkingPin: null,
});

// focuses on a selection of a given tab
export const selectionLens = R.lens(
  R.propOr([], 'selection'),
  R.assoc('selection')
);

// focuses on a linking pin of a given tab
export const linkingPinLens = R.lensProp('linkingPin');

const openPatchByPath = R.curry((patchPath, state) => {
  const alreadyOpened = isPatchOpened(patchPath, state);
  const tabId = alreadyOpened
    ? getTabIdbyPatchPath(patchPath, state)
    : shortid.generate();

  return R.compose(
    R.assoc('currentTabId', tabId),
    R.unless(() => alreadyOpened, addPatchTab(tabId, patchPath))
  )(state);
});

const openLatestOpenedTab = R.converge(R.assoc('currentTabId'), [
  R.compose(
    // get patch id from last of remaining tabs
    R.propOr(null, 'id'),
    R.last,
    R.values,
    R.prop('tabs')
  ),
  R.identity,
]);

const closeTabById = R.curry((tabId, state) => {
  if (!isTabOpened(tabId, state)) return state;

  const tabToClose = getTabById(tabId, state);

  const isDebuggerTabClosing = () => tabToClose.type === TAB_TYPES.DEBUGGER;
  const isCurrentTabClosing = R.propEq('currentTabId', tabId);
  const isCurrentDebuggerTabClosing = R.both(
    isDebuggerTabClosing,
    isCurrentTabClosing
  );

  const openOriginalPatch = patchPath =>
    R.compose(
      R.converge(setTabOffset(tabToClose.offset), [
        getTabIdbyPatchPath(patchPath),
        R.identity,
      ]),
      openPatchByPath(patchPath)
    );

  return R.compose(
    R.cond([
      [isCurrentDebuggerTabClosing, openOriginalPatch(tabToClose.patchPath)],
      [isCurrentTabClosing, openLatestOpenedTab],
      [R.T, R.identity],
    ]),
    R.dissocPath(['tabs', tabId])
  )(state);
});

const closeTabByPatchPath = R.curry((patchPath, state) => {
  const tabIdToClose = getTabIdbyPatchPath(patchPath, state);
  return closeTabById(tabIdToClose, state);
});

// :: PatchPath -> PatchPath -> Tab -> Tab
const renamePatchPathInBreadcrumbs = R.curry(
  (newPatchPath, oldPatchPath, tab) =>
    R.when(
      R.has('breadcrumbs'),
      R.over(
        R.lensPath(['breadcrumbs', 'chunks']),
        R.map(
          R.when(
            R.propEq('patchPath', oldPatchPath),
            R.assoc('patchPath', newPatchPath)
          )
        )
      )
    )(tab)
);

const renamePatchInTabs = (newPatchPath, oldPatchPath, state) => {
  const tabIdsToRename = R.compose(R.map(R.prop('id')), listTabsByPatchPath)(
    oldPatchPath,
    state
  );

  return tabIdsToRename.length === 0
    ? state
    : R.over(
        R.lensProp('tabs'),
        R.map(
          R.compose(
            renamePatchPathInBreadcrumbs(newPatchPath, oldPatchPath),
            R.when(
              R.propEq('patchPath', oldPatchPath),
              R.assoc('patchPath', newPatchPath)
            )
          )
        ),
        state
      );
};

const findChunkIndex = R.curry((patchPath, nodeId, chunks) =>
  R.findIndex(
    R.both(R.propEq('patchPath', patchPath), R.propEq('nodeId', nodeId)),
    chunks
  )
);

const createChunk = (patchPath, nodeId) => ({
  patchPath,
  nodeId,
  offset: DEFAULT_PANNING_OFFSET,
});

const setActiveIndex = R.curry((index, state) => {
  const curTabId = getCurrentTabId(state);
  return R.assocPath(
    ['tabs', curTabId, 'breadcrumbs', 'activeIndex'],
    index,
    state
  );
});

const drillDown = R.curry((patchPath, nodeId, state) => {
  const currentTabId = getCurrentTabId(state);
  if (currentTabId !== DEBUGGER_TAB_ID) return state;

  const activeIndex = getBreadcrumbActiveIndex(state);
  const chunks = getBreadcrumbChunks(state);
  const index = findChunkIndex(patchPath, nodeId, chunks);

  if (index > -1) {
    const chunkOffset = getBreadcrumbChunks(state)[index].offset;
    return R.compose(
      setTabOffset(chunkOffset, currentTabId),
      setActiveIndex(index)
    )(state);
  }

  const shouldResetTail = activeIndex < chunks.length - 1;
  const newChunk = createChunk(patchPath, nodeId);
  // TODO: always default offset, not calculated optimal
  const newChunkOffset = newChunk.offset;

  return R.compose(
    setTabOffset(newChunkOffset, currentTabId),
    R.converge(setActiveIndex, [
      R.compose(R.dec, R.length, getBreadcrumbChunks),
      R.identity,
    ]),
    R.over(
      R.compose(currentTabLens, R.lensPath(['breadcrumbs', 'chunks'])),
      R.compose(
        R.append(newChunk),
        R.when(() => shouldResetTail, R.take(activeIndex + 1))
      )
    )
  )(state);
});

const openDebuggerTab = R.curry((patchPath, state) => {
  const currentTab = getTabById(state.currentTabId, state);
  const currentPatchPath = currentTab.patchPath;
  const currentOffset = currentTab.offset;
  return R.compose(
    setTabOffset(currentOffset, DEBUGGER_TAB_ID),
    drillDown(patchPath, null),
    R.assoc('currentTabId', DEBUGGER_TAB_ID),
    addTabWithProps(DEBUGGER_TAB_ID, TAB_TYPES.DEBUGGER, currentPatchPath)
  )(state);
});

// =============================================================================
//
// Reducer
//
// =============================================================================

const editorReducer = (state = {}, action) => {
  switch (action.type) {
    //
    // selection management
    //
    case PAT.BULK_DELETE_ENTITIES:
    case EAT.EDITOR_DESELECT_ALL:
      return R.over(currentTabLens, clearSelection, state);
    case EAT.EDITOR_SELECT_ENTITY:
      return R.set(
        R.compose(currentTabLens, selectionLens),
        [createSelectionEntity(action.payload.entityType, action.payload.id)],
        state
      );
    case EAT.EDITOR_DESELECT_ENTITY:
      return R.over(
        R.compose(currentTabLens, selectionLens),
        R.reject(
          R.equals(
            createSelectionEntity(action.payload.entityType, action.payload.id)
          )
        ),
        state
      );
    case EAT.EDITOR_ADD_ENTITY_TO_SELECTION:
      return R.over(
        R.compose(currentTabLens, selectionLens),
        R.compose(
          R.uniq,
          R.append(
            createSelectionEntity(action.payload.entityType, action.payload.id)
          )
        ),
        state
      );
    case EAT.EDITOR_SET_SELECION:
      return R.set(
        R.compose(currentTabLens, selectionLens),
        getNewSelection(action.payload.entities),
        state
      );
    case EAT.EDITOR_SELECT_PIN:
      return R.set(
        R.compose(currentTabLens, linkingPinLens),
        action.payload,
        state
      );
    case EAT.EDITOR_DESELECT_PIN:
    case PAT.LINK_ADD:
    case PAT.ADD_BUS_NODE:
      return R.set(R.compose(currentTabLens, linkingPinLens), null, state);

    //
    // Adding entities
    //
    case EAT.PASTE_ENTITIES:
      return R.compose(
        R.set(
          R.compose(currentTabLens, selectionLens),
          getNewSelection(action.payload.entities)
        ),
        R.assoc('focusedArea', FOCUS_AREAS.WORKAREA)
      )(state);
    case PAT.NODE_ADD:
      return R.compose(
        R.set(R.compose(currentTabLens, selectionLens), [
          createSelectionEntity(
            SELECTION_ENTITY_TYPE.NODE,
            action.payload.newNodeId
          ),
        ]),
        R.assoc('focusedArea', FOCUS_AREAS.WORKAREA),
        R.assoc('draggedPreviewSize', { width: 0, height: 0 })
      )(state);

    //
    // tabs management
    //
    case PAT.PROJECT_CREATE: {
      const newState = R.assoc('tabs', {}, state);
      return editorReducer(newState, switchPatchUnsafe(MAIN_PATCH_PATH));
    }
    case PAT.PROJECT_OPEN:
    case PAT.PROJECT_IMPORT: {
      const newState = R.merge(state, {
        currentTabId: null,
        tabs: {},
      });
      return resetCurrentPatchPath(editorReducer, newState, action.payload);
    }
    case PAT.PROJECT_OPEN_WORKSPACE:
      return R.merge(state, {
        currentTabId: null,
        tabs: {},
      });
    case PAT.PATCH_ADD:
    case EAT.EDITOR_SWITCH_PATCH:
      return openPatchByPath(action.payload.patchPath, state);
    case EAT.EDITOR_SWITCH_TAB:
      return R.assoc('currentTabId', action.payload.tabId, state);
    case EAT.EDITOR_OPEN_ATTACHMENT:
      return R.over(
        currentTabLens,
        R.assoc('editedAttachment', action.payload),
        state
      );
    case EAT.EDITOR_CLOSE_ATTACHMENT:
      return R.over(currentTabLens, R.assoc('editedAttachment', null), state);
    case PAT.PATCH_RENAME:
      return renamePatchInTabs(
        action.payload.newPatchPath,
        action.payload.oldPatchPath,
        state
      );
    case PAT.PATCH_DELETE:
      return closeTabByPatchPath(action.payload.patchPath, state);
    case EAT.TAB_CLOSE:
      return closeTabById(action.payload.id, state);
    case EAT.TAB_SORT:
      return R.assoc(
        'tabs',
        R.reduce(
          (p, cur) => R.assoc(cur.id, applyTabSort(cur, action.payload), p),
          {},
          R.values(state.tabs)
        ),
        state
      );
    case EAT.SET_CURRENT_PATCH_OFFSET: {
      return R.compose(
        syncTabOffset(action.payload),
        setTabOffset(action.payload, state.currentTabId)
      )(state);
    }

    //
    // dragging patch from project browser
    //
    case EAT.START_DRAGGING_PATCH:
      return R.assoc('draggedPreviewSize', action.payload, state);

    //
    // size of the patch workarea
    //
    case EAT.PATCH_WORKAREA_RESIZED:
      return R.assoc('patchWorkareaSize', action.payload, state);

    //
    // focused area
    //
    case EAT.SET_FOCUSED_AREA:
      return R.assoc('focusedArea', action.payload, state);

    //
    // helpbox
    //
    case REMOVE_SELECTION:
    case EAT.HIDE_HELPBOX:
      return R.over(R.lensProp('isHelpboxVisible'), R.F, state);
    case EAT.SHOW_HELPBOX:
      return R.over(R.lensProp('isHelpboxVisible'), R.T, state);
    case EAT.TOGGLE_HELP:
      return R.over(R.lensProp('isHelpboxVisible'), R.not, state);

    //
    // suggester
    //
    case EAT.SHOW_SUGGESTER: {
      if (R.path(['suggester', 'visible'], state) === true) return state;

      return R.compose(
        R.over(R.lensProp('isHelpboxVisible'), R.T),
        R.assoc('focusedArea', FOCUS_AREAS.NODE_SUGGESTER),
        R.assocPath(['suggester', 'visible'], true),
        R.assocPath(['suggester', 'placePosition'], action.payload)
      )(state);
    }
    case EAT.HIDE_SUGGESTER:
      return R.compose(
        R.over(R.lensProp('isHelpboxVisible'), R.F),
        R.assocPath(['suggester', 'visible'], false),
        R.assocPath(['suggester', 'highlightedPatchPath'], null),
        R.assocPath(['suggester', 'placePosition'], null)
      )(state);
    case EAT.HIGHLIGHT_SUGGESTER_ITEM:
      return R.assocPath(
        ['suggester', 'highlightedPatchPath'],
        action.payload.patchPath,
        state
      );
    case EAT.SHOW_LIB_SUGGESTER:
      return R.assoc('libSuggesterVisible', true, state);
    case EAT.INSTALL_LIBRARIES_BEGIN:
    case EAT.HIDE_LIB_SUGGESTER:
      return R.assoc('libSuggesterVisible', false, state);

    //
    // debugger
    //
    case DAT.DEBUG_SESSION_STARTED:
      return openDebuggerTab(action.payload.patchPath, state);
    case DAT.DEBUG_SESSION_STOPPED:
      return closeTabById(DEBUGGER_TAB_ID, state);
    case DAT.DEBUG_DRILL_DOWN:
      return R.compose(
        drillDown(action.payload.patchPath, action.payload.nodeId),
        setPropsToTab(DEBUGGER_TAB_ID, { patchPath: action.payload.patchPath }),
        R.over(currentTabLens, clearSelection)
      )(state);

    //
    // panel settings
    //
    case EAT.SET_SIDEBAR_LAYOUT:
      return R.over(R.lensProp('panels'), R.merge(R.__, action.payload), state);
    case EAT.RESIZE_PANELS:
      return R.over(
        R.lensProp('panels'),
        R.mapObjIndexed((val, id) =>
          R.when(
            () => R.contains(id, R.keys(action.payload)),
            R.assoc('size', action.payload[id])
          )(val)
        ),
        state
      );
    case EAT.MINIMIZE_PANEL:
      return R.assocPath(
        ['panels', action.payload.panelId, 'maximized'],
        false,
        state
      );
    case EAT.MAXIMIZE_PANEL:
      return R.assocPath(
        ['panels', action.payload.panelId, 'maximized'],
        true,
        state
      );
    case EAT.MOVE_PANEL:
      return R.compose(
        // hide helpbox panel on moving ProjectBrowser
        // it should be fixed in the future
        // by saving expanded states of PatchGroups
        R.when(
          () => action.payload.panelId === PANEL_IDS.PROJECT_BROWSER,
          R.over(R.lensProp('isHelpboxVisible'), R.F)
        ),
        R.assocPath(
          ['panels', action.payload.panelId, 'sidebar'],
          action.payload.sidebarId
        )
      )(state);
    case EAT.TOGGLE_PANEL_AUTOHIDE:
      return R.over(
        R.lensPath(['panels', action.payload.panelId, 'autohide']),
        R.not,
        state
      );

    case EAT.FOCUS_BOUND_VALUE:
      // maximize Inspector panel to focus & select widget's input
      return R.assocPath(
        ['panels', PANEL_IDS.INSPECTOR, 'maximized'],
        true,
        state
      );

    case EAT.TABTEST_LAUNCHED:
      return R.assoc('tabtestWorker', action.payload.worker, state);
    case EAT.TABTEST_RUN_REQUESTED:
      return R.assoc('isTabtestRunning', true, state);
    case EAT.TABTEST_RUN_FINISHED:
    case EAT.TABTEST_ABORT:
    case EAT.TABTEST_ERROR:
      return R.compose(
        R.assoc('tabtestWorker', null),
        R.assoc('isTabtestRunning', false)
      )(state);

    case EAT.SIMULATION_LAUNCHED:
      return R.compose(
        R.assoc('simulationWorker', action.payload.worker),
        openDebuggerTab(action.payload.patchPath)
      )(state);
    case EAT.SIMULATION_ABORT:
    case EAT.SIMULATION_ERROR:
      return R.compose(
        R.assoc('simulationWorker', null),
        closeTabById(DEBUGGER_TAB_ID)
      )(state);

    case EAT.SHOW_COLORPICKER_WIDGET:
      return R.over(
        R.lensProp('pointingPopups'),
        R.compose(
          R.assocPath(['colorPickerWidget', 'isVisible'], true),
          R.assocPath(
            ['colorPickerWidget', 'elementId'],
            action.payload.elementId
          )
        )
      )(state);
    case EAT.HIDE_COLORPICKER_WIDGET:
      return R.over(
        R.lensProp('pointingPopups'),
        R.compose(
          R.assocPath(['colorPickerWidget', 'isVisible'], false),
          R.assocPath(['colorPickerWidget', 'elementId'], null)
        )
      )(state);

    default:
      return state;
  }
};

export default editorReducer;
