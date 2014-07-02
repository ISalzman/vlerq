package vlerq

var metaMeta, emptyMeta = setupSpecialViews()

type View struct {
	rows uint32
	keys uint8
	uniq uint8
	meta *View
	data []Column
}

func NewView(mv *View) *View {
	if mv == nil {
		mv = metaMeta
	}
	v := new(View)
	v.meta = mv
	v.data = make([]Column, mv.Cols())
	return v
}

func (v *View) Rows() int {
	return int(v.rows)
}

func (v *View) Cols() int {
	return v.meta.Rows()
}

func (v *View) Meta() *View {
	return v.meta
}

type Column interface {
	At(i int) interface{}
	Set(i int, v interface{})
}

type IntColumn []int

func (c IntColumn) At(i int) interface{} {
	return c[i]
}

func (c IntColumn) Set(i int, v interface{}) {
	c[i] = v.(int)
}

type StringColumn []string

func (c StringColumn) At(i int) interface{} {
	return c[i]
}

func (c StringColumn) Set(i int, v interface{}) {
	c[i] = v.(string)
}

type ViewColumn []*View

func (c ViewColumn) At(i int) interface{} {
	return c[i]
}

func (c ViewColumn) Set(i int, v interface{}) {
	c[i] = v.(*View)
}

func setupSpecialViews() (mmv, emv *View) {
	emv = &View{
		rows: 0,
		keys: 1,
		uniq: 1,
		data: []Column{
			StringColumn{},
			StringColumn{},
			ViewColumn{},
		},
	}
	mmv = &View{
		rows: 3,
		keys: 1,
		uniq: 1,
		data: []Column{
			StringColumn{"name", "type", "subv"},
			StringColumn{"S", "S", "V"},
			ViewColumn{emv, emv, emv},
		},
	}
	mmv.meta = mmv // circular reference
	emv.meta = mmv
	return
}
