import R from 'ramda';
import { Maybe, Either } from 'ramda-fantasy';

import * as CONST from './constants';
import * as Project from './project';
import * as Patch from './patch';
import * as Node from './node';
import { explode } from './utils';

// :: Monad a -> Monad a
const reduceChainOver = R.reduce(R.flip(R.chain));

// :: String[] -> Project -> Path -> [Path, Patch, ...]
const extractImplPatches = R.curry((impls, project, path, patch) => R.ifElse(
    Patch.hasImpl(impls),
    patchWithImpl => [path, patchWithImpl],
    R.compose(
      R.chain(R.compose(
        type => Project.getPatchByPath(type, project)
          .chain(extractImplPatches(impls, project, type)),
        Node.getNodeType
      )),
      Patch.listNodes
    )
)(patch));

// :: Node -> Boolean
const isNodeToImplPatch = implPatchPaths => R.compose(
  R.contains(R.__, implPatchPaths),
  Node.getNodeType
);

const duplicateNodePrefixed = R.curry((prefix, node) => R.compose(
  (newNode) => {
    const id = Node.getNodeId(newNode);
    const newId = (prefix.length > 0) ? `${prefix}~${id}` : id;
    return R.assoc('id', newId, newNode);
  },
  JSON.parse,
  JSON.stringify
)(node));

// :: Project -> String[] -> Patch -> Node[]
const extractNodes = R.curry((project, implPatchPaths, parentNodeId, patch) => R.compose(
  R.chain(R.ifElse(
      isNodeToImplPatch(implPatchPaths),
      duplicateNodePrefixed(parentNodeId),
      (node) => {
        const type = Node.getNodeType(node);
        const id = Node.getNodeId(node);
        return Project.getPatchByPath(type, project)
          .chain(extractNodes(project, implPatchPaths, id));
      }
    )
  ),
  Patch.listNodes
)(patch));

export default R.curry((project, path, impls) => {
  const patch = explode(Project.getPatchByPath(path, project));

  // Maybe [path, Patch, path, Patch, ...]
  const implPatches = extractImplPatches(impls, project, path, patch);
  // [[path, Patch], ...]
  const splittedImplPatches = R.splitEvery(2, implPatches);

  if (R.isEmpty(splittedImplPatches)) {
    return Either.Left(new Error(CONST.ERROR.IMPLEMENTATION_NOT_FOUND));
  }

  // [fn, ...]
  const assocImplPatches = splittedImplPatches.map(R.apply(Project.assocPatch));

  const assocPatch = R.ifElse(
    Patch.hasImpl(impls),
    R.always(R.identity),
    (originalPatch) => {
      const implPatchPaths = R.pluck(0, splittedImplPatches);
      const nodes = extractNodes(project, implPatchPaths, '', originalPatch);
      const assocNodes = nodes.map(node => Patch.assocNode(node));
      const newPatch = R.reduce((p, fn) => fn(p), Patch.createPatch(), assocNodes);

      return R.chain(Project.assocPatch(path, newPatch));
    }
  )(patch);

  return R.compose(
    assocPatch,
    reduceChainOver(R.__, assocImplPatches),
    Maybe.of
  )(Project.createProject());
});
