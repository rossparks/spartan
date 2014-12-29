from traits.api import Instance

from .base import ListExpr, TupleExpr, PythonValue, Expr
from ..array import distarray, tile, extent
from ..core import LocalKernelResult
from spartan import rpc
from spartan.node import Node


def outer_mapper(ex, arrays, axes, local_user_fn, local_user_fn_kw, target):
  # Fetch the tile for the first array
  first_ex = extent.change_partition_axis(ex, axes[0])
  first_tile = arrays[0].fetch(ex)

  futures = rpc.FutureGroup()
  if local_user_fn_kw is None:
    local_user_fn_kw = {}

  done_extent = {}
  for key in arrays[1].tiles.iterkeys():
    outer_extent = extent.change_partition_axis(key, axes[1])
    if done_extent.get(outer_extent, None) is not None:
      # Usually, change_partition_axis won't return the same extents
      # (if there is no bugs :) ). However, if the underline array
      # is a vector and the new partition axis is 1, the API will
      # return an extent that cover the whole vector.
      # We need to avoid redo the extent.
      continue

    outer_tile = arrays[1].fetch(outer_extent)
    result = local_user_fn(first_ex, first_tile,
                           outer_extent, outer_tile,
                           **local_user_fn_kw)
    if result is not None:
      for ex, v in result:
        futures.append(target.update(ex, v, wait=False))
    done_extent[outer_extent] = True

  return LocalKernelResult(result=[], futures=futures)


class OuterProductExpr(Expr):
  arrays = Instance(TupleExpr)
  axes = Instance(tuple)
  fn = PythonValue
  fn_kw = PythonValue
  shape = Instance(tuple)
  tile_hint = Instance(tuple)
  reducer = PythonValue

  def pretty_str(self):
    return 'OuterProduct[%d]' % (self.expr_id)

  def compute_shape(self):
    return self.shape

  def _evaluate(self, ctx, deps):
    arrays = deps['arrays']
    axes = deps['axes']
    fn = deps['fn']
    fn_kw = deps['fn_kw']
    shape = deps['shape']
    tile_hint = deps['tile_hint']
    reducer = deps['reducer']

    target = distarray.create(shape, arrays[0].dtype,
                              sharder=None, reducer=reducer,
                              tile_hint=tile_hint,
                              sparse=arrays[0].sparse)

    arrays[0].foreach_tile(mapper_fn=outer_mapper,
                           kw=dict(arrays=arrays, axes=axes, local_user_fn=fn,
                                   local_user_fn_kw=fn_kw, target=target))
    return target


def outer(arrays, axes, fn, fn_kw=None,
          shape=None, tile_hint=None, reducer=None):
  '''
  Outer (cartesian) product over the tiles of ``a`` and ``b``.

  ``map_fn`` is applied to each pair; ``reduce_fn`` is used to
  combine overlapping outputs.

  :param arrays:
  :param axes:
  '''
  assert fn is not None

  arrays = TupleExpr(vals=arrays)

  return OuterProductExpr(arrays=arrays, axes=axes, fn=fn, fn_kw=fn_kw,
                          shape=tuple(shape), tile_hint=tile_hint,
                          reducer=reducer)
