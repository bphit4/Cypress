package dynasty

import (
	"encoding/xml"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
)

type Catalog struct {
	Root               string
	FileCount          int
	SchemaCount        int
	FlowCount          int
	ExprCount          int
	UIRequestFormCount int
	Schemas            map[string]Schema
}

type Schema struct {
	Name         string
	Base         string
	RelativePath string
	Fields       []Field
}

type Field struct {
	Name   string
	IsExpr bool
}

type rawType struct {
	XMLName xml.Name   `xml:"Type"`
	Name    string     `xml:"name,attr"`
	Base    string     `xml:"base,attr"`
	Fields  []rawField `xml:",any"`
}

type rawFranTkData struct {
	Schemas []rawSchema `xml:"schemas>schema"`
}

type rawSchema struct {
	Name   string     `xml:"name,attr"`
	Base   string     `xml:"base,attr"`
	Fields []rawField `xml:"attribute"`
}

type rawField struct {
	XMLName xml.Name
	Name    string `xml:"name,attr"`
	IsExpr  string `xml:"isExpr,attr"`
}

func LoadCatalog(root string) (*Catalog, error) {
	catalog := &Catalog{
		Root:    root,
		Schemas: make(map[string]Schema),
	}

	err := filepath.WalkDir(root, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() || !strings.EqualFold(filepath.Ext(path), ".FTX") {
			return nil
		}

		catalog.FileCount++
		data, err := os.ReadFile(path)
		if err != nil {
			return err
		}

		schemas, err := parseSchemas(data)
		if err != nil {
			return nil
		}
		if len(schemas) == 0 {
			return nil
		}

		rel, err := filepath.Rel(root, path)
		if err != nil {
			rel = path
		}
		for _, raw := range schemas {
			schema := Schema{
				Name:         raw.Name,
				Base:         raw.Base,
				RelativePath: rel,
			}
			for _, field := range raw.Fields {
				if field.Name == "" {
					continue
				}
				isExpr := strings.EqualFold(field.IsExpr, "true")
				if isExpr {
					catalog.ExprCount++
				}
				schema.Fields = append(schema.Fields, Field{Name: field.Name, IsExpr: isExpr})
			}

			catalog.SchemaCount++
			if raw.Base == "FranTkServer_Flow" {
				catalog.FlowCount++
			}
			if raw.Base == "UIRequestForm" {
				catalog.UIRequestFormCount++
			}
			catalog.Schemas[raw.Name] = schema
		}
		return nil
	})
	if err != nil {
		return nil, err
	}
	return catalog, nil
}

func parseSchemas(data []byte) ([]rawSchema, error) {
	var probe struct {
		XMLName xml.Name
	}
	if err := xml.Unmarshal(data, &probe); err != nil {
		return nil, err
	}

	if probe.XMLName.Local == "Type" {
		var raw rawType
		if err := xml.Unmarshal(data, &raw); err != nil {
			return nil, err
		}
		if raw.Name == "" {
			return nil, nil
		}
		return []rawSchema{{Name: raw.Name, Base: raw.Base, Fields: raw.Fields}}, nil
	}

	var ftx rawFranTkData
	if err := xml.Unmarshal(data, &ftx); err != nil {
		return nil, err
	}
	return ftx.Schemas, nil
}
