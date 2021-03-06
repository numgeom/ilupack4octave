function test_aes_fem

[~, output] = system('gd-ls -p 0ByTwsK5_Tl_PdDVSWUZzUmxpMWs "Neumann_*_d=[246].mat"');

files = strsplit(output, '\n');
rtol = 1.e-12;

for i=1:length(files)
    fname = strtrim(files{i});
    if ~isempty(fname)
        system(['gd-get -O -p 0ByTwsK5_Tl_PdDVSWUZzUmxpMWs ' fname]);
        s = load(fname);

        fprintf(1, 'Solving %s\n', fname);
        A = crs_matrix(s.aes_fe3_linsys.row_ptr, ...
            s.aes_fe3_linsys.col_ind, s.aes_fe3_linsys.val);

        b = s.aes_fe3_linsys.b;
        x = gmresMILU(A, b, 'maxit', 500, 'rtol', rtol, ...
            'verb', 2, 'condest', 5, 'droptol', 0.001);
        fprintf(1, 'Relative residual is %1.g, with tolerance %1.g\n', ...
            norm(crs_prodAx(A, x) - b) / norm(b), rtol);
    end
end

end
