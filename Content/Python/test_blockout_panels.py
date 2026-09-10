"""
Verification par PROPRIETES du noyau geometrique de panneau (BlockoutTools).

POURQUOI CE FICHIER EXISTE, ET POURQUOI IL A ETE REECRIT
---------------------------------------------------------
Il verifiait deja le noyau C++ dans RPG_Test. Lors de la fusion du 2026-08-29 je l'ai classe a
tort parmi les tests des modules Python legacy et supprime : il ne testait pas du Python
obsolete, il testait du C++ VIVANT, par un pont UFUNCTION. Il n'avait jamais ete commite, donc
il etait perdu. Reecrit ici a partir des proprietes decrites dans Docs/Guide_Dessin_Blockout.md.

Il ne verifie pas une liste de resultats figes mais des PROPRIETES qui doivent tenir pour
n'importe quelle entree. C'est ce qui lui permet d'attraper des bugs qu'aucun cas d'exemple
n'aurait revele.

C'est le SECOND chemin de verification. Le premier est le harnais hors moteur
Plugins/BlockoutTools/Tools/GeometryTests, qui exerce le meme noyau sans Unreal. Deux chemins
independants sur le meme code : une erreur commune aux deux est bien plus improbable qu'une
erreur dans l'un.

UTILISATION :
    import test_blockout_panels; test_blockout_panels.run_all()

Resultat aussi ecrit dans <Projet>/Saved/Tests/blockout_panels_result.json
"""

import json
import math
import os
import unreal

TOL_AIRE = 1e-3          # tolerance relative sur les aires
TOL_EPAISSEUR = 1e-3     # tolerance relative sur l'epaisseur du prisme


def _sub():
    return unreal.get_editor_subsystem(unreal.BlockoutGeometrySubsystem)


def _result_path():
    d = os.path.join(
        unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Tests")
    if not os.path.isdir(d):
        os.makedirs(d)
    return os.path.abspath(os.path.join(d, "blockout_panels_result.json"))


def _v2(x, y):
    return unreal.Vector2D(x, y)


def _rect(w, h, cx=0.0, cy=0.0):
    return [_v2(cx - w / 2, cy - h / 2), _v2(cx + w / 2, cy - h / 2),
            _v2(cx + w / 2, cy + h / 2), _v2(cx - w / 2, cy + h / 2)]


def _hole(points):
    h = unreal.BlockoutPanelHole()
    h.set_editor_property("points", points)
    return h


def _aire(loop):
    """Aire absolue d'un polygone ferme (formule du lacet)."""
    n = len(loop)
    s = 0.0
    for i in range(n):
        a, b = loop[i], loop[(i + 1) % n]
        s += a.x * b.y - b.x * a.y
    return abs(s) * 0.5


def _build(outer, thickness, holes, u=0, v=1, w=2):
    """Construit un panneau via le pont UFUNCTION.

    Le retour est un tuple de TROIS tableaux (sommets, indices, UV) : la valeur booleenne de la
    fonction C++ n'apparait pas cote Python. Verifie par introspection plutot que suppose -- une
    erreur d'arite ici faisait echouer sept tests sur neuf avec une trace tronquee.
    Le succes se lit donc a la presence de triangles.
    """
    res = _sub().build_panel_mesh_for_test(outer, thickness, holes, u, v, w)
    verts, tris, uvs = list(res[0]), list(res[1]), list(res[2])
    return (len(tris) > 0), verts, tris, uvs


def _aire_triangulee(verts, tris, u, v, w=2):
    """Aire des triangles d'UNE seule face, projetee dans le plan (U,V).

    Le maillage est recto-verso et comporte aussi les parois d'extrusion. Une premiere version
    sommait les aires signees positives en esperant que la face arriere sorte negative : elle
    mesurait exactement le DOUBLE de l'attendu dans les cinq cas concernes -- les deux faces se
    projettent du meme cote. Le facteur 2 net est d'ailleurs ce qui a montre que le noyau etait
    juste et le test faux.

    On ne retient donc que les triangles entierement poses sur la face avant (W maximal). Les
    parois laterales, elles, ont une aire projetee nulle et ne fausseraient rien -- mais les
    exclure ainsi rend la mesure independante de leur presence.
    """
    if not verts or not tris:
        return 0.0

    def coord(p, axis):
        return (p.x, p.y, p.z)[axis]

    wmax = max(coord(p, w) for p in verts)
    seuil = max(abs(wmax), 1.0) * 1e-6

    # Chaque facette est emise DEUX FOIS, recto et verso, aux memes positions (normales posees
    # a la main, recalcul moteur desactive -- voir Guide_Dessin_Blockout.md). Filtrer par hauteur
    # ne les separe donc pas : les deux copies sont au meme W. On deduplique par POSITIONS, ce
    # qui compte chaque facette une seule fois quel que soit son sens d'enroulement.
    vues = set()
    total = 0.0
    for i in range(0, len(tris), 3):
        a, b, c = verts[tris[i]], verts[tris[i + 1]], verts[tris[i + 2]]
        if (abs(coord(a, w) - wmax) > seuil or abs(coord(b, w) - wmax) > seuil
                or abs(coord(c, w) - wmax) > seuil):
            continue

        au, av = coord(a, u), coord(a, v)
        bu, bv = coord(b, u), coord(b, v)
        cu, cv = coord(c, u), coord(c, v)

        cle = tuple(sorted([(round(au, 4), round(av, 4)),
                            (round(bu, 4), round(bv, 4)),
                            (round(cu, 4), round(cv, 4))]))
        if cle in vues:
            continue
        vues.add(cle)

        total += abs((bu - au) * (cv - av) - (cu - au) * (bv - av)) * 0.5
    return total


class Resultats(object):
    def __init__(self):
        self.tests = []

    def check(self, nom, ok, detail=""):
        self.tests.append({"nom": nom, "passe": bool(ok), "detail": str(detail)})
        return bool(ok)


# ---------------------------------------------------------------------------
# Proprietes
# ---------------------------------------------------------------------------

def _p_aire(r):
    """L'aire triangulee vaut l'aire du contour moins celle des trous."""
    cas = [
        ("plein", _rect(400, 300), []),
        ("un trou", _rect(400, 300), [_rect(80, 60)]),
        ("deux trous", _rect(400, 300), [_rect(60, 40, -100, 0), _rect(60, 40, 100, 0)]),
    ]
    for nom, outer, holes in cas:
        ok, verts, tris, _ = _build(outer, 20.0, [_hole(h) for h in holes])
        if not ok:
            r.check("aire / %s" % nom, False, "construction refusee")
            continue
        attendu = _aire(outer) - sum(_aire(h) for h in holes)
        mesure = _aire_triangulee(verts, tris, 0, 1)
        ecart = abs(mesure - attendu) / max(attendu, 1.0)
        r.check("aire / %s" % nom, ecart <= TOL_AIRE,
                "attendu %.1f, mesure %.1f (ecart relatif %.2e)" % (attendu, mesure, ecart))


def _p_couverture(r):
    """Un point dans la matiere est dans le contour et hors des trous ; l'inverse dans un trou."""
    outer = _rect(400, 300)
    trou = _rect(80, 60)
    s = _sub()
    dans_matiere = s.point_in_contour(_v2(150, 100), outer) and not s.point_in_contour(_v2(150, 100), trou)
    dans_trou = s.point_in_contour(_v2(0, 0), outer) and s.point_in_contour(_v2(0, 0), trou)
    dehors = not s.point_in_contour(_v2(500, 0), outer)
    r.check("couverture / point dans la matiere", dans_matiere)
    r.check("couverture / point dans le trou", dans_trou)
    r.check("couverture / point hors panneau", dehors)


def _p_epaisseur(r):
    """Le maillage est un prisme dont l'epaisseur est exactement celle demandee."""
    for ep in (5.0, 20.0, 137.5):
        ok, verts, tris, _ = _build(_rect(300, 200), ep, [_hole(_rect(50, 50))])
        if not ok:
            r.check("epaisseur / %g" % ep, False, "construction refusee")
            continue
        ws = [vv.z for vv in verts]
        mesure = max(ws) - min(ws)
        ecart = abs(mesure - ep) / ep
        r.check("epaisseur / %g" % ep, ecart <= TOL_EPAISSEUR,
                "mesure %.4f (ecart relatif %.2e)" % (mesure, ecart))


def _p_degenere(r):
    """Aucun triangle ne se replie sur une aire negligeable, seuil RELATIF a la taille."""
    outer = _rect(400, 300)
    ok, verts, tris, _ = _build(outer, 20.0, [_hole(_rect(80, 60))])
    if not ok:
        r.check("triangles degeneres", False, "construction refusee")
        return
    seuil = _aire(outer) * 1e-9
    pires = 0
    for i in range(0, len(tris), 3):
        a, b, c = verts[tris[i]], verts[tris[i + 1]], verts[tris[i + 2]]
        ab = unreal.Vector(b.x - a.x, b.y - a.y, b.z - a.z)
        ac = unreal.Vector(c.x - a.x, c.y - a.y, c.z - a.z)
        cr = ab.cross(ac)
        if 0.5 * math.sqrt(cr.x ** 2 + cr.y ** 2 + cr.z ** 2) <= seuil:
            pires += 1
    r.check("triangles degeneres", pires == 0, "%d triangle(s) sous le seuil" % pires)


def _p_determinisme(r):
    """Deux appels identiques rendent la meme surface, au bit pres."""
    args = (_rect(400, 300), 20.0, [_hole(_rect(80, 60))])
    ok1, v1, t1, _ = _build(*args)
    ok2, v2, t2, _ = _build(*args)
    memes = ok1 and ok2 and len(v1) == len(v2) and len(t1) == len(t2) and t1 == t2
    if memes:
        memes = all(abs(a.x - b.x) + abs(a.y - b.y) + abs(a.z - b.z) == 0.0 for a, b in zip(v1, v2))
    r.check("determinisme", memes, "%d/%d sommets, %d/%d indices" % (len(v1), len(v2), len(t1), len(t2)))


def _p_validations(r):
    """Les gardes de contour refusent ce qu'elles doivent refuser."""
    s = _sub()
    croise = [_v2(0, 0), _v2(100, 100), _v2(100, 0), _v2(0, 100)]
    r.check("garde / contour croise refuse", not s.is_simple_contour(croise))
    r.check("garde / contour simple accepte", s.is_simple_contour(_rect(100, 100)))
    r.check("garde / trou interieur accepte",
            s.contour_contains_with_margin(_rect(400, 300), _rect(50, 50), 0.001))
    r.check("garde / trou debordant refuse",
            not s.contour_contains_with_margin(_rect(400, 300), _rect(500, 50), 0.001))
    r.check("garde / trous disjoints",
            not s.contours_overlap(_rect(40, 40, -100, 0), _rect(40, 40, 100, 0)))
    r.check("garde / trous superposes",
            s.contours_overlap(_rect(60, 60, 0, 0), _rect(60, 60, 20, 20)))


# ---------------------------------------------------------------------------
# Regressions -- chacune correspond a un bug reellement rencontre cote Unity
# ---------------------------------------------------------------------------

def _r_invariance_position(r):
    """Un panneau translate donne la meme aire : la geometrie ne depend pas de l'origine."""
    a_ok, av, at, _ = _build(_rect(400, 300), 20.0, [_hole(_rect(80, 60))])
    b_ok, bv, bt, _ = _build(_rect(400, 300, 5000, -3000), 20.0, [_hole(_rect(80, 60, 5000, -3000))])
    if not (a_ok and b_ok):
        r.check("regression / invariance a la position", False, "construction refusee")
        return
    a1 = _aire_triangulee(av, at, 0, 1)
    a2 = _aire_triangulee(bv, bt, 0, 1)
    ecart = abs(a1 - a2) / max(a1, 1.0)
    r.check("regression / invariance a la position", ecart <= TOL_AIRE,
            "%.1f vs %.1f (ecart %.2e)" % (a1, a2, ecart))


def _r_arete_quasi_horizontale(r):
    """Trou dont une arete est presque horizontale, pres du bord : cas casse-triangulation."""
    outer = _rect(400, 300)
    trou = [_v2(-50, 0.0), _v2(50, 0.0001), _v2(50, 40), _v2(-50, 40)]
    ok, verts, tris, _ = _build(outer, 20.0, [_hole(trou)])
    if not ok:
        r.check("regression / arete quasi-horizontale", False, "construction refusee")
        return
    attendu = _aire(outer) - _aire(trou)
    mesure = _aire_triangulee(verts, tris, 0, 1)
    ecart = abs(mesure - attendu) / attendu
    r.check("regression / arete quasi-horizontale", ecart <= TOL_AIRE,
            "attendu %.1f, mesure %.1f" % (attendu, mesure))


def _r_vingt_et_un_trous(r):
    """21 ouvertures dans un meme mur : la degradation ne doit pas s'accumuler."""
    outer = _rect(2200, 400)
    trous, aire_trous = [], 0.0
    for i in range(21):
        t = _rect(60, 60, -1000 + i * 100, 0)
        trous.append(_hole(t))
        aire_trous += _aire(t)
    ok, verts, tris, _ = _build(outer, 20.0, trous)
    if not ok:
        r.check("regression / 21 ouvertures", False, "construction refusee")
        return
    attendu = _aire(outer) - aire_trous
    mesure = _aire_triangulee(verts, tris, 0, 1)
    ecart = abs(mesure - attendu) / attendu
    r.check("regression / 21 ouvertures", ecart <= TOL_AIRE,
            "attendu %.1f, mesure %.1f (ecart %.2e)" % (attendu, mesure, ecart))


# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Trace a angles droits (Ctrl en mode dessin) -- SnapToOrthogonalFrame
# ---------------------------------------------------------------------------

def _snap(last, cand, frame, incoming=(0.0, 0.0)):
    """`incoming` = direction du segment qui arrive sur `last`. (0,0) desactive la
    regle anti-repli et n'exerce que le choix de l'axe dominant."""
    v = _sub().snap_to_orthogonal_frame(_v2(*last), _v2(*cand), _v2(*frame), _v2(*incoming))
    return (v.x, v.y)


def _angle_entre(a, b, c):
    """Angle en degres au sommet b, entre les segments [a,b] et [b,c]."""
    ux, uy = a[0] - b[0], a[1] - b[1]
    vx, vy = c[0] - b[0], c[1] - b[1]
    nu = math.hypot(ux, uy)
    nv = math.hypot(vx, vy)
    if nu < 1e-9 or nv < 1e-9:
        return None
    cos = max(-1.0, min(1.0, (ux * vx + uy * vy) / (nu * nv)))
    return math.degrees(math.acos(cos))


def _p_ortho_angle_droit(r):
    """La propriete demandee : tout mur adjacent forme un angle droit.

    On ne teste pas la formule, on teste la CONSEQUENCE -- l'angle reellement obtenu
    entre deux murs consecutifs. Une erreur de signe ou d'axe passerait un test qui
    se contente de comparer a une valeur attendue, pas celui-ci.
    """
    reperes = [(100.0, 0.0), (0.0, 100.0), (70.0, 70.0), (-30.0, 80.0), (12.0, -5.0)]
    candidats = [(250.0, 40.0), (-180.0, 220.0), (30.0, -400.0), (5.0, 5.0), (-2.0, 300.0)]

    pires = []
    for frame in reperes:
        for cand in candidats:
            a = (0.0, 0.0)                 # avant-dernier point
            b = (frame[0], frame[1])       # dernier point pose = bout du 1er segment
            c = _snap(b, (b[0] + cand[0], b[1] + cand[1]), frame, incoming=frame)
            ang = _angle_entre(a, b, c)
            if ang is None:
                continue
            # 90 (virage) ou 180 (on continue tout droit) : dans les deux cas aucun
            # mur adjacent ne forme un angle autre que droit.
            ecart = min(abs(ang - 90.0), abs(ang - 180.0))
            pires.append((ecart, frame, cand, round(ang, 4)))

    pires.sort(reverse=True)
    pire = pires[0]
    r.check("ortho / angle reellement a 90 ou 180", pire[0] < 1e-6,
            "pire ecart %.2e deg (repere %s, candidat %s -> angle %s)" % pire)


def _p_ortho_dominante(r):
    """Le segment suit l'axe vers lequel la souris est le plus partie."""
    frame = (100.0, 0.0)          # repere = axe X ; perpendiculaire = axe Y
    last = (10.0, 20.0)
    cas = [
        ("surtout en X", (310.0, 25.0), "x"),
        ("surtout en Y", (15.0, 420.0), "y"),
        ("X negatif",    (-290.0, 12.0), "x"),
        ("Y negatif",    (13.0, -260.0), "y"),
    ]
    for nom, cand, axe in cas:
        sx, sy = _snap(last, cand, frame)
        if axe == "x":
            ok = abs(sy - last[1]) < 1e-9 and abs(sx - cand[0]) < 1e-9
        else:
            ok = abs(sx - last[0]) < 1e-9 and abs(sy - cand[1]) < 1e-9
        r.check("ortho / dominante %s" % nom, ok, "snap = (%.3f, %.3f)" % (sx, sy))


def _p_ortho_invariants(r):
    """Idempotence, point deja aligne, repere degenere, distance non allongee."""
    frame = (80.0, 60.0)
    last = (-40.0, 15.0)
    cand = (200.0, -90.0)

    une = _snap(last, cand, frame)
    deux = _snap(last, une, frame)
    r.check("ortho / idempotent",
            abs(une[0] - deux[0]) < 1e-9 and abs(une[1] - deux[1]) < 1e-9,
            "%s puis %s" % (une, deux))

    # Un point deja sur un axe du repere ne doit pas bouger.
    ux, uy = 0.8, 0.6                       # frame normalise
    deja = (last[0] + ux * 250.0, last[1] + uy * 250.0)
    fixe = _snap(last, deja, frame)
    r.check("ortho / point deja aligne non deplace",
            abs(fixe[0] - deja[0]) < 1e-6 and abs(fixe[1] - deja[1]) < 1e-6,
            "%s -> %s" % (deja, fixe))

    # Repere nul : rien a quoi s'aligner, le candidat doit ressortir intact.
    intact = _snap(last, cand, (0.0, 0.0))
    r.check("ortho / repere degenere rend le candidat",
            abs(intact[0] - cand[0]) < 1e-9 and abs(intact[1] - cand[1]) < 1e-9,
            str(intact))

    # Projeter ne peut pas eloigner : le mur contraint n'est jamais plus long.
    d_avant = math.hypot(cand[0] - last[0], cand[1] - last[1])
    d_apres = math.hypot(une[0] - last[0], une[1] - last[1])
    r.check("ortho / ne rallonge jamais le segment", d_apres <= d_avant + 1e-9,
            "%.3f -> %.3f" % (d_avant, d_apres))


def _p_ortho_pas_de_repli(r):
    """Ctrl ne doit jamais poser un mur par-dessus le precedent.

    Cas trouve par le test d'angle, pas par relecture : avec un repere en biais, la
    composante dominante peut pointer a l'oppose du mur qu'on vient de tracer. L'angle
    valait alors 0 degre et le contour devenait auto-intersectant a la fermeture.
    """
    cas = [
        ("repere en biais",  (12.0, -5.0),  (-180.0, 220.0)),
        ("repli franc en X", (100.0, 0.0),  (-400.0, 30.0)),
        ("repli franc en Y", (0.0, 100.0),  (25.0, -380.0)),
    ]
    for nom, frame, mouvement in cas:
        a = (0.0, 0.0)
        b = (frame[0], frame[1])
        c = _snap(b, (b[0] + mouvement[0], b[1] + mouvement[1]), frame, incoming=frame)
        ang = _angle_entre(a, b, c)
        ok = ang is not None and abs(ang - 90.0) < 1e-6
        r.check("ortho / pas de repli (%s)" % nom, ok,
                "angle %s deg" % (round(ang, 6) if ang is not None else "indefini"))


def _p_ortho_contour_complet(r):
    """Un contour entier trace avec Ctrl : tous ses angles sont droits.

    Repere en biais (le batiment n'est pas aligne sur les axes du monde), ce qui est
    precisement ce que le choix du repere sur le 1er segment doit permettre.
    """
    frame = (60.0, 45.0)                     # ~36.87 degres
    pts = [(0.0, 0.0), (60.0, 45.0)]         # 1er segment libre, il fixe le repere
    souris = [(140.0, -180.0), (-260.0, -190.0), (-150.0, 200.0), (90.0, 260.0)]
    for m in souris:
        last, avant = pts[-1], pts[-2]
        entrant = (last[0] - avant[0], last[1] - avant[1])
        pts.append(_snap(last, (last[0] + m[0], last[1] + m[1]), frame, incoming=entrant))

    pires = []
    for i in range(1, len(pts) - 1):
        ang = _angle_entre(pts[i - 1], pts[i], pts[i + 1])
        if ang is not None:
            pires.append(min(abs(ang - 90.0), abs(ang - 180.0)))
    r.check("ortho / contour en biais, tous les angles droits",
            bool(pires) and max(pires) < 1e-6,
            "%d sommets, pire ecart %.2e deg" % (len(pires), max(pires) if pires else -1))

    # Et le contour reste utilisable par le reste du noyau.
    plat = [_v2(x, y) for (x, y) in pts]
    r.check("ortho / le contour obtenu reste simple",
            bool(_sub().is_simple_contour(plat)), "%d points" % len(plat))


def _fermer(first, frame, before_last):
    """Renvoie (point_ajuste, ok). Le succes est un parametre de sortie, pas la valeur
    de retour : Python ne recupere pas celle-ci quand la UFUNCTION a des out-params.

    L'ancienne position du dernier point n'est pas un argument : elle n'entre pas dans
    le calcul, le point est REMPLACE par celui qui ferme d'equerre."""
    pt, ok = _sub().snap_last_point_to_close_frame(_v2(*first), _v2(*frame), _v2(*before_last))
    return (pt.x, pt.y), bool(ok)


def _p_fermeture_rectangle(r):
    """Sur 4 points d'equerre, l'ajustement doit donner le coin du rectangle."""
    cas = [
        ("aligne sur X/Y", [(0.0, 0.0), (400.0, 0.0), (400.0, 300.0), (120.0, 260.0)]),
        ("repere en biais", [(0.0, 0.0), (300.0, 225.0), (180.0, 385.0), (-90.0, 170.0)]),
        ("sens horaire",    [(0.0, 0.0), (0.0, 400.0), (-300.0, 400.0), (-260.0, 90.0)]),
    ]
    for nom, p in cas:
        frame = (p[1][0] - p[0][0], p[1][1] - p[0][1])
        pt, ok = _fermer(p[0], frame, p[2])
        attendu = (p[0][0] + p[2][0] - p[1][0], p[0][1] + p[2][1] - p[1][1])
        ecart = math.hypot(pt[0] - attendu[0], pt[1] - attendu[1]) if ok else -1.0
        r.check("fermeture / coin du rectangle (%s)" % nom, ok and ecart < 1e-6,
                "ok=%s, obtenu %s, attendu %s, ecart %.2e" % (ok, pt, attendu, ecart))


def _p_fermeture_angles(r):
    """La consequence : le contour ferme n'a plus que des angles droits.

    On mesure les QUATRE angles du contour ferme, y compris les deux qui touchent le
    segment de fermeture -- ce sont eux que l'ajustement doit redresser.
    """
    depart = [(0.0, 0.0), (300.0, 225.0), (180.0, 385.0), (-90.0, 170.0)]
    frame = (depart[1][0] - depart[0][0], depart[1][1] - depart[0][1])
    pt, ok = _fermer(depart[0], frame, depart[2])
    if not r.check("fermeture / ajustement accepte", ok, str(pt)):
        return

    pts = depart[:3] + [pt]
    pires = []
    for i in range(len(pts)):
        a, b, c = pts[i - 1], pts[i], pts[(i + 1) % len(pts)]
        ang = _angle_entre(a, b, c)
        if ang is not None:
            pires.append((abs(ang - 90.0), i, round(ang, 6)))
    pires.sort(reverse=True)
    r.check("fermeture / les 4 angles a 90 degres",
            bool(pires) and pires[0][0] < 1e-6,
            "%d sommets, pire : sommet %d a %s deg" % (len(pires), pires[0][1], pires[0][2]))

    r.check("fermeture / contour toujours simple",
            bool(_sub().is_simple_contour([_v2(x, y) for (x, y) in pts])),
            str(pts))


def _p_fermeture_refus(r):
    """Les cas ou il faut refuser plutot que rendre un point faux."""
    # Repere degenere : aucun axe sur lequel fermer.
    pt, ok = _fermer((0.0, 0.0), (0.0, 0.0), (100.0, 100.0))
    r.check("fermeture / refus si repere nul", not ok, "ok=%s" % ok)

    # L'avant-dernier point est deja sur la droite de fermeture : le dernier mur
    # sortirait de longueur nulle.
    pt, ok = _fermer((0.0, 0.0), (100.0, 0.0), (0.0, 250.0))
    r.check("fermeture / refus si dernier mur nul", not ok, "ok=%s, %s" % (ok, pt))

    # L'avant-dernier point est sur le premier : le segment de fermeture serait nul.
    pt, ok = _fermer((0.0, 0.0), (100.0, 0.0), (0.0, 0.0))
    r.check("fermeture / refus si fermeture nulle", not ok, "ok=%s, %s" % (ok, pt))


def _p_fermeture_limite_annoncee(r):
    """Ce que Ctrl+F ne peut PAS faire, verifie explicitement.

    Il ne deplace qu'un point : il commande donc l'angle de ce point et celui du
    premier. L'angle de l'avant-dernier depend du mur d'AVANT, que Ctrl+F ne touche
    pas -- si le trace precedent n'etait pas d'equerre, cet angle-la ne le sera pas.
    Mieux vaut que ce soit un test qu'une surprise.
    """
    # Deuxieme mur volontairement en biais par rapport au repere.
    p0, p1, p2 = (0.0, 0.0), (400.0, 0.0), (330.0, 300.0)
    pt, ok = _fermer(p0, (400.0, 0.0), p2)
    if not r.check("limite / ajustement accepte", ok, str(pt)):
        return
    pts = [p0, p1, p2, pt]

    a_dernier = _angle_entre(p2, pt, p0)
    a_premier = _angle_entre(pt, p0, p1)
    r.check("limite / les 2 angles commandes sont droits",
            abs(a_dernier - 90.0) < 1e-6 and abs(a_premier - 90.0) < 1e-6,
            "dernier %.4f deg, premier %.4f deg" % (a_dernier, a_premier))

    a_avant = _angle_entre(p1, p2, pt)
    r.check("limite / l'angle non commande reste tel quel",
            abs(a_avant - 90.0) > 1.0,
            "angle a l'avant-dernier point : %.3f deg (attendu : pas droit)" % a_avant)


def _p_fermeture_idempotente(r):
    """Reappliquer la fermeture ne doit plus rien deplacer."""
    depart = [(0.0, 0.0), (400.0, 0.0), (400.0, 300.0), (120.0, 260.0)]
    frame = (400.0, 0.0)
    un, ok1 = _fermer(depart[0], frame, depart[2])
    if not r.check("fermeture / 1er passage", ok1, str(un)):
        return
    deux, ok2 = _fermer(depart[0], frame, depart[2])
    r.check("fermeture / idempotente", ok2 and math.hypot(deux[0] - un[0], deux[1] - un[1]) < 1e-6,
            "%s puis %s" % (un, deux))


def run_all(verbose=True):
    r = Resultats()
    for f in (_p_aire, _p_couverture, _p_epaisseur, _p_degenere, _p_determinisme,
              _p_validations, _r_invariance_position, _r_arete_quasi_horizontale,
              _r_vingt_et_un_trous,
              _p_ortho_angle_droit, _p_ortho_dominante, _p_ortho_invariants,
              _p_ortho_pas_de_repli, _p_ortho_contour_complet,
              _p_fermeture_rectangle, _p_fermeture_angles, _p_fermeture_refus,
              _p_fermeture_limite_annoncee, _p_fermeture_idempotente):
        try:
            f(r)
        except Exception:
            import traceback
            r.check(f.__name__, False, traceback.format_exc()[:300])

    passes = sum(1 for t in r.tests if t["passe"])
    rapport = {"total": len(r.tests), "reussis": passes,
               "tout_vert": passes == len(r.tests), "tests": r.tests}
    rapport["verdict"] = "PASS" if rapport["tout_vert"] else "FAIL"

    with open(_result_path(), "w") as fh:
        json.dump(rapport, fh, indent=2, default=str)

    if verbose:
        for t in r.tests:
            unreal.log("%s %s -- %s" % ("[OK]" if t["passe"] else "[FAIL]", t["nom"], t["detail"]))
    unreal.log("[test_blockout_panels] %s : %d/%d" % (rapport["verdict"], passes, len(r.tests)))
    return rapport["tout_vert"]
