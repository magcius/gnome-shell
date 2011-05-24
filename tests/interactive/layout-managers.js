/* -*- mode: js2; js2-basic-offset: 4; indent-tabs-mode: nil -*- */

const Clutter = imports.gi.Clutter;
const St = imports.gi.St;

const UI = imports.testcommon.ui;

UI.init();
let stage = Clutter.Stage.get_default();
stage.user_resizable = true;

let faces = ['angry', 'angel', 'cool', 'crying',
             'devilish', 'embarrassed', 'glasses',
             'kiss', 'laugh', 'monkey', 'plain',
             'raspberry', 'sad', 'sick', 'smile-big',
             'smile', 'smirk', 'surprise', 'uncertain',
             'wink', 'worried'];

let vbox = new St.BoxLayout({ style: 'padding: 10px; background: #ffee88;' });
vbox.add_constraint(new Clutter.BindConstraint({ source: stage,
                                                 coordinate: Clutter.BindCoordinate.SIZE }));
stage.add_actor(vbox);

let scroll = new St.ScrollView();
vbox.add(scroll, { expand: true });

let box = new St.BoxLayout({ vertical: true,
                             style: 'spacing: 20px;' });
scroll.add_actor(box);

function getTable() {
    let layout = new Clutter.TableLayout({ row_spacing: 0,
                                           column_spacing: 32 });
    let facetable = new St.LayoutContainer({ layout_manager: layout,
                                             style: 'padding: 5px; border: 25px solid green' });

    let width = 4; // 4 items per row

    for (let i = 0; i < faces.length; i ++) {
        let face = new St.Icon({ icon_name: 'face-' + faces[i],
                                 icon_type: St.IconType.FULLCOLOR,
                                 icon_size: 48 });

        let y = Math.floor(i/width);
        let x = Math.floor(i%width);

        let special = (x == (y % width));
        let colspan = special ? 2 : 1;

        if (x > (y % width))
            x ++;

        if (special)
            face.set_style("background-color: rgba(255, 0, 0, 0.5);");

        facetable.add(face, { column: x,
                              row: y,
                              column_span: colspan });
    }

    return facetable;
}

function getFlow() {
    let layout = new Clutter.FlowLayout({ row_spacing: 0,
                                          column_spacing: 8 });
    let faceflow = new St.LayoutContainer({ layout_manager: layout,
                                            style: 'padding: 5px; border: 50px solid blue' });
    faceflow.width = 500;

    for (let i = 0; i < faces.length; i ++) {
        let face = new St.Icon({ icon_name: 'face-' + faces[i],
                                 icon_type: St.IconType.FULLCOLOR,
                                 icon_size: 48 });

        faceflow.add(face);
    }

    return faceflow;
}

box.add_actor(getTable());
box.add_actor(getFlow());

////////////////////////////////////////////////////////////////////////////////

stage.show();
Clutter.main();
