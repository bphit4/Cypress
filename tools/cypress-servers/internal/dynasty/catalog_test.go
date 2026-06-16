package dynasty

import (
	"os"
	"path/filepath"
	"testing"
)

func TestCatalogLoadsFTXSchemaTypes(t *testing.T) {
	root := t.TempDir()
	write := func(rel, body string) {
		path := filepath.Join(root, rel)
		if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, []byte(body), 0644); err != nil {
			t.Fatal(err)
		}
	}

	write("core-schemas/uirequestform.FTX", `<Type name="UIRequestForm" base="Request"></Type>`)
	write("franchise-schemas/advancestagerequest.FTX", `<Type name="AdvanceStageRequest" base="UIRequestForm"></Type>`)
	write("franchise-schemas/franchiseserver_careerflow.FTX", `<Type name="FranchiseServer_CareerFlow" base="FranTkServer_Flow"><Field name="IssueAdvanceStageRequest" isExpr="true" /></Type>`)

	catalog, err := LoadCatalog(root)
	if err != nil {
		t.Fatal(err)
	}

	if catalog.FileCount != 3 {
		t.Fatalf("expected 3 files, got %d", catalog.FileCount)
	}
	if catalog.SchemaCount != 3 {
		t.Fatalf("expected 3 schemas, got %d", catalog.SchemaCount)
	}
	if catalog.UIRequestFormCount != 1 {
		t.Fatalf("expected 1 UI request form, got %d", catalog.UIRequestFormCount)
	}
	if catalog.FlowCount != 1 {
		t.Fatalf("expected 1 flow, got %d", catalog.FlowCount)
	}
	if catalog.ExprCount != 1 {
		t.Fatalf("expected 1 expression, got %d", catalog.ExprCount)
	}
	if _, ok := catalog.Schemas["AdvanceStageRequest"]; !ok {
		t.Fatalf("expected AdvanceStageRequest in catalog")
	}
}

