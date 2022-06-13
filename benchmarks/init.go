package benchmarks

import (
	"io/ioutil"

	"github.com/endlessxxoo/recastnavigation-go/tests"
)

const RAND_MAX_COUNT int = 20000000
const PATH_MAX_NODE int = 2048

var tempdata1 []byte
var tempdata2 []byte
var mesh1 *detour.DtNavMesh
var mesh2 *detour.DtNavMesh
var tilecache2 *dtcache.DtTileCache

func init() {
	tempdata1, _ = ioutil.ReadFile("../tests/randpos.tile.bin")
	tempdata2, _ = ioutil.ReadFile("../tests/randpos.tilecache.bin")
	mesh1 = tests.LoadStaticMesh("../tests/scene1.obj.tile.bin")
	mesh2, tilecache2 = tests.LoadDynamicMesh("../tests/scene1.obj.tilecache.bin")
	detour.DtIgnoreUnused(tilecache2)
}
