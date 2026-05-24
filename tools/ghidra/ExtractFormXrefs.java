// Ghidra headless script for extracting ordinary-form serializer xrefs.
//
// Usage through analyzeHeadless:
//   -postScript ExtractFormXrefs.java <output-json>
//
// The script intentionally emits only symbol/xref metadata. Platform binaries
// stay outside the repository and should be mounted from an ignored workdir.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import java.io.File;
import java.io.PrintWriter;

public class ExtractFormXrefs extends GhidraScript {
    private static String esc(String value) {
        if (value == null) {
            return "";
        }
        return value
            .replace("\\", "\\\\")
            .replace("\"", "\\\"")
            .replace("\n", "\\n")
            .replace("\r", "\\r");
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : "/work/out/form-xrefs.json");
        out.getParentFile().mkdirs();

        String[] needles = new String[] {
            "cf_form_controls8",
            "cf_form_controls_position8",
            "cf_form_controls_info8",
            "ListInStream",
            "ListOutStream",
            "TypeDomainPattern",
            "CompositeID",
            "serialize",
            "deserialize"
        };

        try (PrintWriter pw = new PrintWriter(out, "UTF-8")) {
            pw.println("{");
            pw.println("  \"program\": \"" + esc(currentProgram.getName()) + "\",");
            pw.println("  \"symbols\": [");
            boolean firstSymbol = true;
            for (String needle : needles) {
                SymbolIterator iterator = currentProgram.getSymbolTable().getSymbolIterator();
                while (iterator.hasNext()) {
                    Symbol symbol = iterator.next();
                    String name = symbol.getName(true);
                    if (!name.contains(needle)) {
                        continue;
                    }
                    if (!firstSymbol) {
                        pw.println(",");
                    }
                    firstSymbol = false;

                    Address address = symbol.getAddress();
                    Function function = getFunctionContaining(address);
                    pw.print(
                        "    {\"needle\":\"" + esc(needle)
                            + "\",\"name\":\"" + esc(name)
                            + "\",\"address\":\"" + address
                            + "\",\"function\":\"" + esc(function == null ? "" : function.getName(true))
                            + "\",\"xrefs\":["
                    );

                    boolean firstRef = true;
                    for (Reference ref : getReferencesTo(address)) {
                        if (!firstRef) {
                            pw.print(",");
                        }
                        firstRef = false;
                        Function fromFunction = getFunctionContaining(ref.getFromAddress());
                        pw.print(
                            "{\"from\":\"" + ref.getFromAddress()
                                + "\",\"type\":\"" + esc(ref.getReferenceType().toString())
                                + "\",\"function\":\""
                                + esc(fromFunction == null ? "" : fromFunction.getName(true))
                                + "\"}"
                        );
                    }
                    pw.print("]}");
                }
            }
            pw.println();
            pw.println("  ]");
            pw.println("}");
        }

        println("Wrote " + out.getAbsolutePath());
    }
}
