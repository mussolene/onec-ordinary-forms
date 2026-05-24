// Ghidra headless script for decompiling ordinary-form serializer candidates.
//
// Usage through analyzeHeadless:
//   -postScript ExtractFormDecompile.java <output-json> [address-or-function ...]
//
// The script emits decompiled C for a narrow, explicit set of functions. Keep
// platform binaries and generated decompile output outside the repository.

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import java.io.File;
import java.io.PrintWriter;
import java.util.LinkedHashMap;
import java.util.Map;

public class ExtractFormDecompile extends GhidraScript {
    private static final String[] DEFAULT_TARGETS = new String[] {
        "FUN_002709e0",
        "FUN_00270da0",
        "FUN_00270fe0",
        "FUN_002c9430",
        "FUN_00255f70",
        "FUN_00256510"
    };

    private static String esc(String value) {
        if (value == null) {
            return "";
        }
        return value
            .replace("\\", "\\\\")
            .replace("\"", "\\\"")
            .replace("\n", "\\n")
            .replace("\r", "\\r")
            .replace("\t", "\\t");
    }

    private Function functionBySymbol(String target) {
        FunctionIterator iterator = currentProgram.getFunctionManager().getFunctions(true);
        while (iterator.hasNext()) {
            Function function = iterator.next();
            if (function.getName().equals(target) || function.getName(true).equals(target)) {
                return function;
            }
        }
        return null;
    }

    private Function functionByAddress(String target) {
        try {
            Address address = toAddr(target);
            Function function = getFunctionAt(address);
            if (function != null) {
                return function;
            }
            return getFunctionContaining(address);
        } catch (Exception ignored) {
            return null;
        }
    }

    private Function resolveFunction(String target) {
        Function byAddress = functionByAddress(target);
        if (byAddress != null) {
            return byAddress;
        }
        return functionBySymbol(target);
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : "/work/out/form-decompile.json");
        out.getParentFile().mkdirs();

        String[] targets;
        if (args.length > 1) {
            targets = new String[args.length - 1];
            System.arraycopy(args, 1, targets, 0, args.length - 1);
        } else {
            targets = DEFAULT_TARGETS;
        }

        DecompInterface decompiler = new DecompInterface();
        DecompileOptions options = new DecompileOptions();
        options.grabFromProgram(currentProgram);
        decompiler.setOptions(options);
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Failed to open program in decompiler");
        }

        Map<String, Function> functions = new LinkedHashMap<>();
        for (String target : targets) {
            Function function = resolveFunction(target);
            if (function != null) {
                functions.put(function.getEntryPoint().toString(), function);
            }
        }

        try (PrintWriter pw = new PrintWriter(out, "UTF-8")) {
            pw.println("{");
            pw.println("  \"program\": \"" + esc(currentProgram.getName()) + "\",");
            pw.println("  \"targets\": [");
            boolean firstTarget = true;
            for (String target : targets) {
                Function function = resolveFunction(target);
                if (!firstTarget) {
                    pw.println(",");
                }
                firstTarget = false;
                pw.print("    {\"requested\":\"" + esc(target) + "\",\"resolved\":\""
                    + esc(function == null ? "" : function.getName(true)) + "\",\"address\":\""
                    + esc(function == null ? "" : function.getEntryPoint().toString()) + "\"}");
            }
            pw.println();
            pw.println("  ],");
            pw.println("  \"functions\": [");

            boolean firstFunction = true;
            for (Function function : functions.values()) {
                DecompileResults results = decompiler.decompileFunction(function, 120, monitor);
                String c = "";
                String error = "";
                if (results.decompileCompleted() && results.getDecompiledFunction() != null) {
                    c = results.getDecompiledFunction().getC();
                } else {
                    error = results.getErrorMessage();
                }

                if (!firstFunction) {
                    pw.println(",");
                }
                firstFunction = false;
                pw.print(
                    "    {\"name\":\"" + esc(function.getName(true))
                        + "\",\"address\":\"" + function.getEntryPoint()
                        + "\",\"signature\":\"" + esc(function.getSignature().toString())
                        + "\",\"body\":\"" + esc(c)
                        + "\",\"error\":\"" + esc(error)
                        + "\"}"
                );
            }

            pw.println();
            pw.println("  ]");
            pw.println("}");
        } finally {
            decompiler.dispose();
        }

        println("Wrote " + out.getAbsolutePath());
    }
}
