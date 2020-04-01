package main

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"sort"
	"strconv"
	"strings"
)

func min(l, r int) int {
	if l > r {
		return r
	}
	return l
}

func toUint(s string) uint {
	i, err := strconv.ParseUint(s, 16, 32)
	if err != nil {
		log.Fatalf("%v", err)
	}
	return uint(i)
}

func mapString(f func(string) string, in []string) []string {
	ret := make([]string, len(in))
	for i, e := range in {
		ret[i] = f(e)
	}
	return ret
}

func sliceStrEql(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}

	for i, e := range a {
		if len(b) <= i || b[i] != e {
			return false
		}
	}
	return true
}

// Join adjacent fields with the same properties
func coalesc(in []field) []field {
	var ret []field
	for _, e := range in {
		tail := len(ret) - 1
		if tail >= 0 && sliceStrEql(ret[tail].prop, e.prop) && ret[tail].span.high+1 == e.span.low {
			ret[tail].span.high = e.span.high
		} else {
			ret = append(ret, e)
		}
	}
	return ret
}

// More aggressively join fields even across gaps in their character sets.
func coalescGapped(in []field) []field {
	// Just ensure these are sorted for correctness
	sortFields(in)
	var ret []field
	for _, e := range in {
		tail := len(ret) - 1
		if tail >= 0 && sliceStrEql(ret[tail].prop, e.prop) {
			ret[tail].span.high = e.span.high
		} else {
			ret = append(ret, e)
		}
	}
	return ret
}

type span struct {
	low  uint
	high uint
}

type field struct {
	span span
	prop []string
}

type ucdScript struct {
	name   string
	fourcc string
	alias  string
}

func readUCD(path string, field_handler func([]string)) {
	f, err := os.Open(path)
	if err != nil {
		log.Fatalf("%v", err)
	}
	defer f.Close()

	r := bufio.NewReader(f)
	for {
		l, err := r.ReadString('\n')
		if err != nil {
			if errors.Is(err, io.EOF) {
				break
			}
			log.Fatalf("%v", err)
		}
		l = strings.TrimSpace(l)

		// Skip empty and comment lines
		if len(l) < 1 || l[0] == '#' {
			continue
		}

		// trim comments
		if ind := strings.Index(l, "#"); ind > -1 {
			l = l[0:ind]
		}
		// split value from property
		split := strings.Split(l, ";")
		if len(split) < 2 {
			log.Fatalf("Line only had 1 component")
		}
		// Might need to explode multi-value properties.
		p := mapString(strings.TrimSpace, split)
		field_handler(p)
	}
}

func parseSpan(val string) span {
	// convert range into low/high
	s := span{0, 0}
	if ind := strings.Index(val, ".."); ind > 0 {
		s.low = toUint(val[:ind])
		s.high = toUint(val[ind+2:])
	} else {
		s.low = toUint(val)
		s.high = s.low
	}
	return s
}

func readCharSetUCD(path string) []field {
	fld := []field{}
	readUCD(path, func(p []string) {
		fld = append(fld, field{span: parseSpan(p[0]), prop: p[1:]})
	})
	return fld
}

func hasOverlap(a, b span) bool {
	return a.low > b.low && a.low < b.high || a.high > b.low && a.high < b.high
}

func validate(f []field) bool {
	for i, e := range f {
		for _, e2 := range f[i:] {
			if hasOverlap(e.span, e2.span) {
				return false
			}
		}
	}
	return true
}

func fieldOrder(f1, f2 field) bool {
	return f1.span.low < f2.span.low
}

type fieldSorter struct{ f []field }

func (fs *fieldSorter) Len() int           { return len(fs.f) }
func (fs *fieldSorter) Less(i, j int) bool { return fieldOrder(fs.f[i], fs.f[j]) }
func (fs *fieldSorter) Swap(i, j int)      { fs.f[i], fs.f[j] = fs.f[j], fs.f[i] }

func sortFields(f []field) {
	fs := &fieldSorter{f: f}
	sort.Sort(fs)
}

func fourccToInt(fourcc string) int {
	if len(fourcc) != 4 {
		log.Fatalf("invalid fourcc string: %s", fourcc)
	}
	r := []rune(fourcc)
	return int(r[0])<<24 + int(r[1])<<16 + int(r[2])<<8 + int(r[3])
}

// dumb table building logic.
func buildTable(tabName, memName, prefix string, f []field) string {
	var out strings.Builder
	fmt.Fprintf(&out, `static const struct {
	uint32_t l;
	uint32_t h;
	uint32_t %s;
} %s[] = {
`, memName, tabName)
	for _, e := range f {
		fmt.Fprintf(&out, "\t{ %0#6x, %0#6x, %s%v },\n", e.span.low, e.span.high, prefix, strings.ToUpper(e.prop[0]))
	}
	fmt.Fprint(&out, "};\n")
	return out.String()
}

func buildWidthTable(prefix string, f []field, nameMap map[string]string) string {
	var out strings.Builder
	fmt.Fprintf(&out, `static const struct {
	uint32_t l;
	uint32_t h;
	uint32_t ea;
} ea_table[] = {
`)
	for _, e := range f {
		fmt.Fprintf(&out, "\t{ %0#6x, %0#6x, %s%s },\n", e.span.low, e.span.high, prefix, strings.ToUpper(nameMap[e.prop[0]]))
	}
	fmt.Fprint(&out, "};\n")
	return out.String()
}

func buildScriptEnum(sc []ucdScript) string {
	var out strings.Builder
	fmt.Fprintf(&out, "enum gpScripts {\n")
	for _, s := range sc {
		fmt.Fprintf(&out, "\tGP_SCRIPT_%s = %0#x,\n", strings.ToUpper(s.name), fourccToInt(s.fourcc))
	}
	fmt.Fprintf(&out, "};\n")
	return out.String()
}

func buildWidthEnum(ea [][]string) string {
	var out strings.Builder
	fmt.Fprintf(&out, "enum gpWidth {\n")
	for _, e := range ea {
		// use full name instead of short for now.
		fmt.Fprintf(&out, "\tGP_WIDTH_%s,\n", strings.ToUpper(e[1]))
	}
	fmt.Fprintf(&out, "};\n")
	return out.String()
}

/*
	script_found_map := make(map[string]bool)
	for _, e := range script {
		script_found_map[e.prop[0]] = true
	}
	var script_found []string
	for k, _ := range script_found_map {
		script_found = append(script_found, k)
	}
*/

func main() {

	var script_names []ucdScript
	// https://www.unicode.org/Public/UCD/latest/ucd/PropertyValueAliases.txt
	readUCD("PropertyValueAliases.txt", func(p []string) {
		if p[0] != "sc" {
			return
		}
		sn := ucdScript{name: p[2], fourcc: p[1]}
		if len(p) > 3 {
			sn.alias = p[3]
		}
		script_names = append(script_names, sn)
	})
	var width_names [][]string
	width_map := make(map[string]string)
	// https://www.unicode.org/Public/UCD/latest/ucd/PropertyValueAliases.txt
	readUCD("PropertyValueAliases.txt", func(p []string) {
		if p[0] != "ea" {
			return
		}
		width_names = append(width_names, []string{p[1], p[2]})
		width_map[p[1]] = p[2]
	})
	// https://www.unicode.org/Public/UCD/latest/ucd/Scripts.txt
	script := readCharSetUCD("Scripts.txt")
	// https://www.unicode.org/Public/UCD/latest/ucd/EastAsianWidth.txt
	width := readCharSetUCD("EastAsianWidth.txt")

	// fmt.Printf("scripts Size: %d, valid: %v\n", len(script), validate(script))
	// fmt.Printf("width Size: %d, valid: %v\n", len(width), validate(width))

	script = coalesc(script)
	width = coalesc(width)
	// fmt.Printf("scripts coalesc Size: %d, valid: %v\n", len(script), validate(script))
	// fmt.Printf("width coalesc Size: %d, valid: %v\n", len(width), validate(width))

	script = coalescGapped(script)
	width = coalescGapped(width)
	// fmt.Printf("scripts aggressive Size: %d, valid: %v\n", len(script), validate(script))
	// fmt.Printf("width aggresive Size: %d, valid: %v\n", len(width), validate(width))

	// fmt.Print(buildScriptEnum(script_names))
	// fmt.Print(buildWidthEnum(width_names))

	fmt.Print(buildWidthTable("GP_WIDTH_", width, width_map))
	fmt.Print(buildTable("sc_table", "sc", "GP_SCRIPT_", script))
}
